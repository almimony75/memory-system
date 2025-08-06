#pragma once

#include "llama.hpp"
#include "crow.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <fstream>

// Faiss includes
#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>

using json = nlohmann::json;

struct MemoryEntry
{
  long id;
  std::string timestamp;
  std::string role;
  std::string content;
};

void to_json(json &j, const MemoryEntry &m);
void from_json(const json &j, MemoryEntry &m);

class MemoryManager
{
public:
  MemoryManager(const std::string &model_path, int dimension = 768);
  ~MemoryManager();

  void add(const std::string &role, const std::string &content);
  std::vector<MemoryEntry> getRelevantMemories(const std::string &query, int k);
  std::vector<MemoryEntry> getLastN(int n);
  size_t getShortTermSize() const;

private:
  std::atomic<bool> dirty_{false};
  std::atomic<bool> stop_saving_{false};
  std::thread saver_thread_;

  std::string model_path_;
  int dimension_;
  long next_id_ = 0;
  std::unique_ptr<LlamaEmbeddingGenerator> embedding_generator_;

  faiss::IndexFlatL2 *index_;
  std::unordered_map<long, MemoryEntry> memory_data_;
  std::deque<long> short_term_ids_;
  std::mutex mtx_;

  const std::string text_file_path = "memory_history_text.json";
  const std::string faiss_index_path = "faiss_index.bin";

  std::string currentTimestamp() const;
  std::vector<float> generateEmbedding(const std::string &text) const;
  void saveToDisk();
  void loadFromDisk();
};