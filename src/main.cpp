#include "crow.h"
#include "MemoryManager.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

const std::string MODEL_PATH = "./nomic-embed-text-v2-moe.f32.gguf";

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

int main()
{
  crow::App<AuthMiddleware> app;
  MemoryManager mem(MODEL_PATH, 768);

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
        } catch (const std::exception& e) {
            std::cerr << "Error in /memory/add: " << e.what() << std::endl;
            return crow::response(400, R"({"status":"error","message":"Invalid JSON"})");
        } });

  // GET /memory/retrieve/recent?last=N
  CROW_ROUTE(app, "/memory/retrieve/recent").methods("GET"_method)([&mem](const crow::request &req)
                                                                   {
        int last = 0;
        if (req.url_params.get("last")) {
            try {
                last = std::stoi(req.url_params.get("last"));
                if (last < 0) throw std::invalid_argument("negative");
            } catch (const std::exception& e) {
                return crow::response(400, R"({"status":"error","message":"Invalid 'last' parameter: must be an integer"})");
            }
        } else {
            last = mem.getShortTermSize();
        }
        auto entries = mem.getLastN(last);
        return crow::response(json(entries).dump(2)); });

  // GET /memory/retrieve/semantic?query=...&k=...
  CROW_ROUTE(app, "/memory/retrieve/semantic").methods("GET"_method)([&mem](const crow::request &req)
                                                                     {
        std::string query_text;
        int k = 5; // Default value
        if (req.url_params.get("query")) {
            query_text = req.url_params.get("query");
        } else {
            return crow::response(400, R"({"status":"error","message":"Missing 'query' parameter"})");
        }
        if (req.url_params.get("k")) {
            try {
                k = std::stoi(req.url_params.get("k"));
                if (k < 1) throw std::invalid_argument("invalid k");
            } catch (const std::exception&) {
                return crow::response(400, R"({"status":"error","message":"Invalid 'k' parameter: must be a positive integer"})");
            }
        }

        auto entries = mem.getRelevantMemories(query_text, k);
        return crow::response(json(entries).dump(2)); });

  app.port(9004).multithreaded().run();
}
