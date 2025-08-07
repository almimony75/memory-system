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
#include <atomic>
#include <thread>

// Remove FAISS includes
// #include <faiss/IndexFlat.h>
// #include <faiss/index_io.h>

// Add hnswlib include
#include "hnswlib/hnswlib.h"

using json = nlohmann::json;

struct MemoryEntry
{
    long id;
    std::string timestamp;
    std::string role;
    std::string content;
};

enum class TaskType
{
    Query,
    Document
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
    int dimension_ = 768;
    long next_id_ = 0;
    std::unique_ptr<LlamaEmbeddingGenerator> embedding_generator_;

    // Replace FAISS pointer with hnswlib index and space pointers
    hnswlib::HierarchicalNSW<float> *index_ = nullptr;
    hnswlib::SpaceInterface<float> *space_ = nullptr;

    std::unordered_map<long, MemoryEntry> memory_data_;
    std::deque<long> short_term_ids_;
    std::mutex mtx_;

    // Increased max_elements capacity for index - you can tune this in the .cpp constructor
    size_t max_elements_;

    // Paths updated to reflect hnswlib usage
    const std::string text_file_path = "memory_data.json";
    const std::string hnsw_index_path = "memory_index.hnsw";

    std::string currentTimestamp() const;
    std::vector<float> MemoryManager::generateEmbedding(const std::string &text, TaskType type) const;
    void saveToDisk();
    void loadFromDisk();
};