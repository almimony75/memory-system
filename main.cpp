#include "crow.h"
#include "MemoryManager.hpp"

// Note: Replace these with your actual paths
const std::string LLAMA_BIN_PATH = "./llama-embedding";
const std::string MODEL_PATH = "./nomic-embed-text-v1.5.f32.gguf";

// --------- Auth Middleware -----------
struct AuthMiddleware
{
    struct context {};
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
    MemoryManager mem(LLAMA_BIN_PATH, MODEL_PATH);

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
        }
    });

    // GET /memory/retrieve/recent?last=N
    CROW_ROUTE(app, "/memory/retrieve/recent").methods("GET"_method)([&mem](const crow::request &req)
    {
        int last = 0;
        if (req.url_params.get("last")) {
            try {
                last = std::stoi(req.url_params.get("last"));
                if (last < 0) throw std::invalid_argument("negative");
            } catch (...) {
                return crow::response(400, R"({"status":"error","message":"Invalid 'last' parameter: must be an integer"})");
            }
        } else {
            // Corrected logic: if 'last' is not specified, get all entries in short-term memory
            last = mem.getShortTermSize();
        }
        auto entries = mem.getLastN(last);
        return crow::response(json(entries).dump(2));
    });

    // GET /memory/retrieve/semantic?query=...&k=...
    CROW_ROUTE(app, "/memory/retrieve/semantic").methods("GET"_method)([&mem](const crow::request &req)
    {
        std::string query_text;
        int k = 5; // Default to 5 relevant memories
        if (req.url_params.get("query")) {
            query_text = req.url_params.get("query");
        } else {
            return crow::response(400, R"({"status":"error","message":"Missing 'query' parameter"})");
        }
        if (req.url_params.get("k")) {
            try {
                k = std::stoi(req.url_params.get("k"));
                if (k < 1) throw std::invalid_argument("invalid k");
            } catch (...) {
                return crow::response(400, R"({"status":"error","message":"Invalid 'k' parameter: must be a positive integer"})");
            }
        }
        
        auto entries = mem.getRelevantMemories(query_text, k);
        return crow::response(json(entries).dump(2));
    });
    
    app.port(9004).multithreaded().run();
}