#include "crow.h"
#include <nlohmann/json.hpp>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <fstream>
#include <chrono>

using json = nlohmann::json;

// --------- MemoryEntry Struct ---------
struct MemoryEntry
{
  std::string timestamp;
  std::string role;
  std::string content;
};

void to_json(json &j, const MemoryEntry &m)
{
  j = json{{"timestamp", m.timestamp}, {"role", m.role}, {"content", m.content}};
}
void from_json(const json &j, MemoryEntry &m)
{
  j.at("timestamp").get_to(m.timestamp);
  j.at("role").get_to(m.role);
  j.at("content").get_to(m.content);
}

// --------- Auth Middleware -----------
struct AuthMiddleware
{
  struct context
  {
  };
  void before_handle(crow::request &req, crow::response &res, context &)
  {
    static constexpr auto AUTH_TOKEN = "super_secret_token_for_prototype";
    auto token = req.get_header_value("X-Auth");
    if (token != AUTH_TOKEN)
    {
      res.code = 401;
      res.set_header("Content-Type", "application/json");
      res.write(R"({"status":"error","message":"unauthorized"})");
      res.end();
    }
  }
  void after_handle(crow::request &, crow::response &, context &) {}
};

// --------- Memory Manager -----------
class MemoryManager
{
public:
  MemoryManager(size_t max_entries = 500, size_t flush_every = 100, int flush_interval_ms = 1000)
      : max_entries_(max_entries), flush_every_(flush_every), flush_interval_ms_(flush_interval_ms),
        unsaved_count_(0), stop_flag_(false)
  {
    loadFromDisk();
    writer_thread_ = std::thread(&MemoryManager::writerLoop, this);
  }

  ~MemoryManager()
  {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      stop_flag_ = true;
    }
    cv_.notify_all();
    if (writer_thread_.joinable())
      writer_thread_.join();
  }

  void add(const std::string &role, const std::string &content)
  {
    bool need_flush = false;
    {
      std::lock_guard<std::mutex> lock(mtx_);
      memory_.emplace_back(MemoryEntry{currentTimestamp(), role, content});
      if (memory_.size() > max_entries_)
        memory_.pop_front();
      unsaved_count_++;
      if (unsaved_count_ >= flush_every_)
      {
        need_flush = true;
      }
    }
    if (need_flush)
      cv_.notify_one();
  }

  std::vector<MemoryEntry> getLastN(int n)
  {
    std::lock_guard<std::mutex> lock(mtx_);
    int count = std::clamp(n, 0, int(memory_.size()));
    std::vector<MemoryEntry> result;
    result.reserve(count);
    auto it = memory_.end();
    for (int i = 0; i < count; ++i)
    {
      --it;
      result.push_back(*it);
    }
    std::reverse(result.begin(), result.end());
    return result;
  }

private:
  std::deque<MemoryEntry> memory_;
  size_t max_entries_, flush_every_, unsaved_count_;
  int flush_interval_ms_;
  std::mutex mtx_;
  std::condition_variable cv_;
  std::thread writer_thread_;
  std::atomic<bool> stop_flag_;
  std::string filename_ = "memory_history.json";

  std::string currentTimestamp() const
  {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&t);
    char buf[30];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
  }

  void writerLoop()
  {
    while (true)
    {
      std::deque<MemoryEntry> mem_copy;
      bool should_exit = false;
      {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::milliseconds(flush_interval_ms_), [this]
                     { return unsaved_count_ >= flush_every_ || stop_flag_; });
        if (unsaved_count_ == 0 && !stop_flag_)
          continue;
        mem_copy = memory_;
        unsaved_count_ = 0;
        should_exit = stop_flag_;
      }
      // Write to disk outside lock
      try
      {
        std::ofstream out(filename_);
        if (out.is_open())
        {
          json j = mem_copy;
          out << j.dump(2);
        }
      }
      catch (...)
      {
        // Optionally handle/log errors
      }
      if (should_exit)
        break;
    }
  }

  void loadFromDisk()
  {
    std::ifstream in(filename_);
    if (in.is_open())
    {
      json j;
      in >> j;
      memory_ = j.get<std::deque<MemoryEntry>>();
    }
  }
};

// --------- Main Crow App -----------
int main()
{
  crow::App<AuthMiddleware> app;
  MemoryManager mem;

  // POST /memory/add
  CROW_ROUTE(app, "/memory/add").methods("POST"_method)([&mem](const crow::request &req)
                                                        {
        try {
            auto body = json::parse(req.body);
            if (!body.is_object() || !body.contains("role") || !body["role"].is_string() ||
                !body.contains("content") || !body["content"].is_string()) {
                return crow::response(400, R"({"status":"error","message":"Invalid request body: 'role' and 'content' required"})");
            }
            mem.add(body["role"], body["content"]);
            return crow::response(200, R"({"status":"success","message":"Memory entry added"})");
        } catch (...) {
            return crow::response(400, R"({"status":"error","message":"Invalid JSON"})");
        } });

  // GET /memory/retrieve?last=N
  CROW_ROUTE(app, "/memory/retrieve").methods("GET"_method)([&mem](const crow::request &req)
                                                            {
        int last = mem.getLastN(0).size(); // default: all
        if (req.url_params.get("last")) {
            try {
                last = std::stoi(req.url_params.get("last"));
                if (last < 0) throw std::invalid_argument("negative");
            } catch (...) {
                return crow::response(400, R"({"status":"error","message":"Invalid 'last' parameter: must be an integer"})");
            }
        }
        auto entries = mem.getLastN(last);
        return crow::response(json(entries).dump(2)); });

  app.port(5073).multithreaded().run();
}
