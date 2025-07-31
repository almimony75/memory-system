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

// Utility function to execute a command and capture its output
std::string exec(const char *cmd)
{
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, void (*)(FILE *)> pipe(popen(cmd, "r"), [](FILE *p)
                                               { pclose(p); });
  if (!pipe)
  {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
  {
    result += buffer.data();
  }
  return result;
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

MemoryManager::MemoryManager(const std::string &llama_bin_path, const std::string &model_path, int dimension)
    : llama_bin_path_(llama_bin_path), model_path_(model_path), dimension_(dimension)
{
  index_ = new faiss::IndexFlatL2(dimension_);
  loadFromDisk();
}

MemoryManager::~MemoryManager()
{
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

  saveToDisk();
}

std::vector<float> MemoryManager::generateEmbedding(const std::string &text) const
{
  std::string text_with_prefix = "search_query: " + text;

  std::string command = llama_bin_path_ +
                        " -ngl 99" +
                        " -m " + model_path_ +
                        " -c 8192" +
                        " -b 8192" +
                        " --rope-scaling yarn" +
                        " --rope-freq-scale 0.75" +
                        " -p \"" + text_with_prefix + "\"";
  std::cout << "Running command: " << command << std::endl;
  std::string output = exec(command.c_str());

  std::vector<float> embedding;
  std::string line;
  std::istringstream iss(output);

  
  while (std::getline(iss, line))
  {
    if (line.find("embedding 0:") != std::string::npos)
    {
      std::istringstream embedding_stream(line.substr(line.find(":") + 1));
      float value;
      while (embedding_stream >> value)
      {
        embedding.push_back(value);
      }
      break; 
    }
  }

  if (embedding.empty() || embedding.size() != dimension_)
  {
    std::cerr << "Command was: " << command << std::endl;
    std::cerr << "Output was: " << output << std::endl;
    throw std::runtime_error("Failed to parse embedding from binary output.");
  }

  return embedding;
}

std::vector<MemoryEntry> MemoryManager::getRelevantMemories(const std::string &query, int k)
{
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<MemoryEntry> results;

  if (index_->ntotal == 0)
  {
    return results;
  }

  try
  {
    std::vector<float> query_embedding = generateEmbedding(query);

    std::vector<long> I(k);
    std::vector<float> D(k);
    index_->search(1, query_embedding.data(), k, D.data(), I.data());

    for (long id : I)
    {
      if (memory_data_.count(id))
      {
        results.push_back(memory_data_[id]);
      }
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