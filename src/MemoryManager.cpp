#include "MemoryManager.hpp"
#include "hnswlib/hnswlib.h" // Added for hnswlib cosine similarity
#include <sstream>
#include <iostream>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <unordered_set>
#include <cmath> // for sqrt

// JSON serialization of MemoryEntry
void to_json(json &j, const MemoryEntry &m)
{
  j = json{
      {"id", m.id},
      {"timestamp", m.timestamp},
      {"role", m.role},
      {"content", m.content}};
}

void from_json(const json &j, MemoryEntry &m)
{
  j.at("id").get_to(m.id);
  j.at("timestamp").get_to(m.timestamp);
  j.at("role").get_to(m.role);
  j.at("content").get_to(m.content);
}

// Utility: Normalize vector to unit length (required for cosine similarity)
static void normalizeVector(std::vector<float> &vec)
{
  float norm = 0.0f;
  for (float v : vec)
    norm += v * v;
  norm = std::sqrt(norm);
  if (norm > 1e-6f)
  {
    for (auto &v : vec)
      v /= norm;
  }
}

std::string MemoryManager::currentTimestamp() const
{
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm = *std::gmtime(&t);
  char buf[30];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

size_t MemoryManager::getShortTermSize() const
{
  return short_term_ids_.size();
}

MemoryManager::MemoryManager(const std::string &model_path, int dimension)
    : model_path_(model_path), dimension_(dimension)
{
  embedding_generator_ = std::make_unique<LlamaEmbeddingGenerator>(model_path_, 512);

  // HNSWlib initialization for cosine similarity
  max_elements_ = 20000; // Adjust to your expected dataset size
  space_ = new hnswlib::InnerProductSpace(dimension_);
  index_ = new hnswlib::HierarchicalNSW<float>(space_, max_elements_, 32, 400); // M=16, efConstruction=400

  loadFromDisk();

  // Background save thread for persistence (unchanged)
  saver_thread_ = std::thread([this]()
                              {
        while (!stop_saving_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (dirty_) {
                std::lock_guard<std::mutex> lock(mtx_);
                saveToDisk();
                dirty_ = false;
            }
        } });
}

MemoryManager::~MemoryManager()
{
  stop_saving_ = true;
  if (saver_thread_.joinable())
    saver_thread_.join();

  std::lock_guard<std::mutex> lock(mtx_);
  if (dirty_)
    saveToDisk();

  // Clean up hnswlib objects
  delete index_;
  delete space_;
}

// Add entry and embedding to memory and index
void MemoryManager::add(const std::string &role, const std::string &content)
{
  std::lock_guard<std::mutex> lock(mtx_);

  long current_id = next_id_++;
  MemoryEntry new_entry = {current_id, currentTimestamp(), role, content};
  memory_data_[current_id] = new_entry;

  short_term_ids_.push_back(current_id);
  if (short_term_ids_.size() > 50)
  {
    short_term_ids_.pop_front();
  }

  try
  {
    // Use TaskType::Document when creating a memory's embedding
    std::vector<float> embedding = generateEmbedding(content, TaskType::Document); // <-- CHANGE HERE

    if (!embedding.empty())
    {
      normalizeVector(embedding); // Normalize embedding for cosine similarity
      index_->addPoint(embedding.data(), current_id);
    }
  }
  catch (const std::runtime_error &e)
  {
    std::cerr << "Error generating embedding: " << e.what() << std::endl;
  }

  dirty_ = true;
}

std::vector<float> MemoryManager::generateEmbedding(const std::string &text, TaskType type) const
{
  std::string prefix;
  if (type == TaskType::Query)
  {
    prefix = "search_query: ";
  }
  else
  {
    prefix = "search_document: ";
  }
  std::string processedText = prefix + text;
  return embedding_generator_->generateEmbedding(processedText);
}

// Retrieve relevant memories with cosine similarity using hnswlib
std::vector<MemoryEntry> MemoryManager::getRelevantMemories(const std::string &query, int k)
{
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<MemoryEntry> results;

  if (index_->cur_element_count == 0 || query.empty())
  {
    return results;
  }

  try
  {
    // Use TaskType::Query when searching
    std::vector<float> query_embedding = generateEmbedding(query, TaskType::Query); // <-- CHANGE HERE
    normalizeVector(query_embedding);                                               // Normalize query embedding for cosine similarity

    size_t search_k = k * 5; // Retrieve more to filter by threshold
    auto knn = index_->searchKnn(query_embedding.data(), search_k);

    std::unordered_set<std::string> seen_content;
    const float BASE_THRESHOLD = 0.75f;
    float dynamic_threshold = BASE_THRESHOLD;
    int i = 0;

    std::vector<std::pair<float, hnswlib::labeltype>> ranked;
    while (!knn.empty())
    {
      ranked.push_back(knn.top());
      knn.pop();
    }
    std::reverse(ranked.begin(), ranked.end());

    for (const auto &item : ranked)
    {
      // Stop once we have enough results
      if (results.size() >= (size_t)k)
        break;

      float dist = item.first;

      // Check if the result is within our new, more permissive threshold
      if (dist <= dynamic_threshold)
      {
        auto doc_id = item.second;
        if (memory_data_.count(doc_id))
        {
          const auto &entry = memory_data_[doc_id];
          // Add the result if we haven't seen this exact content before
          if (seen_content.insert(entry.content).second)
          {
            results.push_back(entry);
          }
        }
      }
    }

    std::cout << "Query: '" << query << "' Final threshold: " << dynamic_threshold
              << " Results found: " << results.size() << "/" << k << "\n";
    if (!ranked.empty())
    {
      std::cout << "Best match cosine distance: " << ranked[0].first << "\n";
    }
  }
  catch (const std::runtime_error &e)
  {
    std::cerr << "Error during semantic search: " << e.what() << std::endl;
  }

  return results;
}

std::vector<MemoryEntry> MemoryManager::getLastN(int n)
{
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<MemoryEntry> result;
  int count = std::clamp(n, 0, (int)short_term_ids_.size());

  auto it = short_term_ids_.rbegin();
  for (int i = 0; i < count; ++i, ++it)
  {
    if (memory_data_.count(*it))
    {
      result.push_back(memory_data_[*it]);
    }
  }
  std::reverse(result.begin(), result.end());
  return result;
}

void MemoryManager::saveToDisk()
{
  std::string hnsw_index_path = "memory_index.hnsw";
  std::string text_file_path = "memory_data.json";

  // Save HNSW index
  index_->saveIndex(hnsw_index_path);

  try
  {
    std::ofstream out(text_file_path);
    if (out.is_open())
    {
      json j = json::array();
      for (const auto &pair : memory_data_)
      {
        j.push_back(pair.second);
      }
      out << j.dump(2);
    }
  }
  catch (...)
  {
    std::cerr << "Error saving memory text data to disk." << std::endl;
  }
}

void MemoryManager::loadFromDisk()
{
  std::string hnsw_index_path = "memory_index.hnsw";
  std::string text_file_path = "memory_data.json";

  if (std::filesystem::exists(hnsw_index_path))
  {
    delete index_;
    index_ = new hnswlib::HierarchicalNSW<float>(space_, hnsw_index_path);
    std::cout << "Loaded HNSW index with "
              << index_->cur_element_count << " vectors." << std::endl;
  }
  else
  {
    std::cerr << "HNSW index file not found. Creating a new one." << std::endl;
    delete index_;
    index_ = new hnswlib::HierarchicalNSW<float>(space_, max_elements_, 16, 200);
  }

  std::ifstream in(text_file_path);
  if (in.is_open())
  {
    json j;
    in >> j;
    if (j.is_array())
    {
      for (const auto &item : j)
      {
        MemoryEntry entry = item.get<MemoryEntry>();
        memory_data_[entry.id] = entry;

        if (entry.id >= next_id_)
        {
          next_id_ = entry.id + 1;
        }
      }
    }
  }
}
