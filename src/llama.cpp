#include "llama.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>

LlamaEmbeddingGenerator::LlamaEmbeddingGenerator(const std::string &model_path, int n_ctx)
    : model_(nullptr), ctx_(nullptr)
{
  static bool backend_initialized = false;
  if (!backend_initialized)
  {
    llama_backend_init();
    backend_initialized = true;
  }

  llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = 99;
  model_ = llama_model_load_from_file(model_path.c_str(), model_params);
  if (!model_)
  {
    throw std::runtime_error("Failed to load model: " + model_path);
  }

  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = n_ctx;
  ctx_params.embeddings = true;
  ctx_params.n_batch = 512;
  ctx_params.n_threads = 4;

  ctx_ = llama_init_from_model(model_, ctx_params);
  if (!ctx_)
  {
    llama_model_free(model_);
    throw std::runtime_error("Failed to create llama context");
  }

  n_embd_ = llama_model_n_embd(model_);
  std::cout << "Model loaded. Embedding dimension: " << n_embd_ << std::endl;
}

LlamaEmbeddingGenerator::~LlamaEmbeddingGenerator()
{
  if (ctx_)
    llama_free(ctx_);
  if (model_)
    llama_model_free(model_);
}

std::vector<float> LlamaEmbeddingGenerator::generateEmbedding(const std::string &text) const
{
  std::lock_guard<std::mutex> lock(generation_mutex_);

  if (text.empty())
  {
    return std::vector<float>(n_embd_, 0.0f);
  }

  // Tokenize the text
  std::vector<llama_token> tokens;
  tokens.resize(text.size() + 16); // Reserve some extra space

  int n_tokens = llama_tokenize(
      llama_model_get_vocab(model_),
      text.c_str(),
      text.size(),
      tokens.data(),
      tokens.size(),
      true,
      false);

  if (n_tokens < 0)
  {

    tokens.resize(-n_tokens);
    n_tokens = llama_tokenize(
        llama_model_get_vocab(model_),
        text.c_str(),
        text.size(),
        tokens.data(),
        tokens.size(),
        true,
        false);
  }

  if (n_tokens <= 0)
  {
    throw std::runtime_error("Failed to tokenize text");
  }

  tokens.resize(n_tokens);

  // Create batch
  llama_batch batch = llama_batch_init(n_tokens, 0, 1);

  for (int i = 0; i < n_tokens; i++)
  {
    batch.token[i] = tokens[i];
    batch.pos[i] = i;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    batch.logits[i] = true;
  }
  batch.n_tokens = n_tokens;

  if (llama_encode(ctx_, batch) != 0)
  {
    llama_batch_free(batch);
    throw std::runtime_error("Failed to encode tokens");
  }

  const float *embeddings = llama_get_embeddings_seq(ctx_, 0);
  if (!embeddings)
  {
    embeddings = llama_get_embeddings(ctx_);
    if (!embeddings)
    {
      llama_batch_free(batch);
      throw std::runtime_error("Failed to get embeddings from context");
    }
  }

  std::vector<float> embedding(embeddings, embeddings + n_embd_);

  // Normalize the embedding vector
  float norm = 0.0f;
  for (float v : embedding)
    norm += v * v;
  norm = std::sqrt(norm);

  if (norm > 1e-12f)
  {
    for (float &v : embedding)
      v /= norm;
  }

  llama_batch_free(batch);
  return embedding;
}