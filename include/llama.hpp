#pragma once

#include "llama.h"
#include <string>
#include <vector>
#include <mutex>

class LlamaEmbeddingGenerator
{
public:
  LlamaEmbeddingGenerator(const std::string &model_path, int n_ctx = 512);
  ~LlamaEmbeddingGenerator();

  // Delete copy constructor and assignment operator
  LlamaEmbeddingGenerator(const LlamaEmbeddingGenerator &) = delete;
  LlamaEmbeddingGenerator &operator=(const LlamaEmbeddingGenerator &) = delete;

  std::vector<float> generateEmbedding(const std::string &text) const;

private:
  llama_model *model_;
  llama_context *ctx_;
  int n_embd_;
  mutable std::mutex generation_mutex_;
};