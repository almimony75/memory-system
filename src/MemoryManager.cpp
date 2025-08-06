#include "MemoryManager.hpp"
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

void to_json(json &j, const MemoryEntry &m)
{
  j = json{{"id", m.id}, {"timestamp", m.timestamp}, {"role", m.role}, {"content", m.content}};
}
void from_json(const json &j, MemoryEntry &m)
{
  j.at("id").get_to(m.id);
  j.at("timestamp").get_to(m.timestamp);
  j.at("role").get_to(m.role);
  j.at("content").get_to(m.content);
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
  index_ = new faiss::IndexFlatL2(dimension_);
  loadFromDisk();

  // Background save thread
  saver_thread_ = std::thread([this]()
                              {
        while (!stop_saving_) {
            std::this_thread::sleep_for(std::chrono::seconds(5)); // save every 5s
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

  delete index_;
}

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
    std::vector<float> embedding = generateEmbedding(content);
    if (!embedding.empty())
    {
      index_->add(1, embedding.data());
    }
  }
  catch (const std::runtime_error &e)
  {
    std::cerr << "Error generating embedding: " << e.what() << std::endl;
  }

  // Mark data as needing save, but don't block here
  dirty_ = true;
}
std::vector<float> MemoryManager::generateEmbedding(const std::string &text) const
{
  // Add query prefix if not already present
  const std::string prefix = "search_query: ";
  std::string processed_text = text;
  if (text.find(prefix) != 0)
  {
    processed_text = prefix + text;
  }
  return embedding_generator_->generateEmbedding(processed_text);
}

std::vector<MemoryEntry> MemoryManager::getRelevantMemories(const std::string &query, int k)
{
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<MemoryEntry> results;

  if (index_->ntotal == 0 || query.empty())
  {
    return results;
  }

  try
  {
    std::vector<float> query_embedding = generateEmbedding(query);

    // Base and maximum threshold values
    const float BASE_THRESHOLD = 1.35f;
    const float MAX_THRESHOLD = 1.6f;
    float dynamic_threshold = BASE_THRESHOLD;

    int search_k = k * 4; // more candidates
    std::vector<long> I(search_k);
    std::vector<float> D(search_k);
    index_->search(1, query_embedding.data(), search_k, D.data(), I.data());

    std::unordered_set<std::string> seen_content;

    for (int i = 0; i < search_k && results.size() < static_cast<size_t>(k); i++)
    {
      if (!memory_data_.count(I[i]))
      {
        continue;
      }

      const auto &entry = memory_data_[I[i]];
      bool content_seen = !seen_content.insert(entry.content).second;

      if (D[i] <= dynamic_threshold && !content_seen)
      {
        results.push_back(entry);
      }
      // Relax threshold if we're not getting enough unique results
      else if (i > k / 2 && results.size() < k / 2)
      {
        dynamic_threshold = std::min(MAX_THRESHOLD, dynamic_threshold * 1.1f);
      }
    }

    // Debug output
    std::cout << "Query: '" << query << "' Final threshold: " << dynamic_threshold
              << " Results found: " << results.size() << "/" << k << "\n";
    if (!results.empty())
    {
      std::cout << "Best match distance: " << D[0] << "\n";
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
  std::string faiss_index_path = "memory_index.faiss";
  std::string text_file_path = "memory_data.json";

  faiss::write_index(index_, faiss_index_path.c_str());

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
  std::string faiss_index_path = "memory_index.faiss";
  std::string text_file_path = "memory_data.json";

  std::ifstream faiss_file(faiss_index_path.c_str(), std::ios::binary);
  if (!faiss_file.is_open())
  {
    std::cerr << "FAISS index file not found. Creating a new one." << std::endl;
    delete index_;
    index_ = new faiss::IndexFlatL2(dimension_);
  }
  else
  {
    try
    {
      faiss::Index *loaded_index = faiss::read_index(faiss_index_path.c_str());
      faiss::IndexFlatL2 *flat_index = dynamic_cast<faiss::IndexFlatL2 *>(loaded_index);

      if (flat_index)
      {
        delete index_;
        index_ = flat_index;
        std::cout << "Loaded Faiss index with " << index_->ntotal << " vectors." << std::endl;
      }
      else
      {
        std::cerr << "Error: Loaded Faiss index is not of type IndexFlatL2. Creating a new one." << std::endl;
        delete loaded_index;
        delete index_;
        index_ = new faiss::IndexFlatL2(dimension_);
      }
    }
    catch (const std::exception &e)
    {
      std::cerr << "Failed to load Faiss index. Error: " << e.what() << ". Creating a new one." << std::endl;
      delete index_;
      index_ = new faiss::IndexFlatL2(dimension_);
    }
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