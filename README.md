## Jarvis Memory Server

An offline, C++ based semantic memory server designed to provide a fast and efficient long-term memory solution for AI assistants like "Jarvis." This server leverages llama.cpp for text embedding generation and Faiss for high-performance similarity search, all running locally without requiring an internet connection.

## Features

- **Offline Operation:** All core functionalities run locally on your machine, ensuring data privacy and low latency.
    
- **Semantic Search:** Utilizes state-of-the-art text embedding models (via `llama.cpp`) and Faiss to find memories based on their meaning, not just keywords.
    
- **Recent Memory Retrieval:** Quickly access the most recently added memories.
    
- **Persistent Storage:** Memories and their embeddings are saved to disk, ensuring data is retained across server restarts.
    
- **RESTful API:** Provides a simple HTTP interface for easy integration with other applications.
    
- **Authentication:** Basic token-based authentication for securing API access.
    
## Prerequisites

To build and run the Jarvis Memory Server, you'll need the following:

- **C++ Compiler:** A C++17 compatible compiler, such as `g++`.
    
- **Faiss Library:** A library for efficient similarity search.
    
- **Crow Web Framework:** A C++ microframework for web development.
    
- **nlohmann/json:** A C++ JSON library.
    
- **llama.cpp:** The `embedding` binary from this project.
    
- **GGUF Embedding Model:** A compatible GGUF model, such as `nomic-embed-text-v1.5.f32.gguf`.
    


## Setup and Compilation

1. **Clone the Repository:**
    
    ```
    git clone https://github.com/almimony75/memory-system.git
    cd memory-system
    ```
    
2. **Update Paths:** Open `main.cpp` and update the `LLAMA_BIN_PATH` and `MODEL_PATH` constants to reflect the actual paths on your system:
    
    ```
    // main.cpp
    const std::string LLAMA_BIN_PATH = "./llama-embedding"; // Example: if binary is in the same dir
    const std::string MODEL_PATH = "./nomic-embed-text-v1.5.f32.gguf"; // Example: if model is in the same dir
    ```
    
3. **Compile the Server:** Use your `run.sh` script to compile the project. This script handles the necessary `g++` flags and library linking.
    
    ```
    ./run.sh
    ```
    
    This will create the `memory_server` executable.
    
## Usage

### Starting the Server

Run the compiled executable:

```
./memory_server
```

The server will start on `http://0.0.0.0:9004`.

### API Endpoints

All API requests require an `X-Auth` header with the value `super_secret_token_for_prototype`.

#### 1. Add Memory Entry

- **Endpoint:** `POST /memory/add`
    
- **Description:** Adds a new memory entry to the system. The content will be embedded and stored for semantic search.
    
- **Request Body (JSON):**
    
    ```
    {
      "role": "user",
      "content": "My friend Emily is a software engineer."
    }
    ```
    
- **Example `curl` command:**
    
    ```
    curl -X POST \
      -H "Content-Type: application/json" \
      -H "X-Auth: super_secret_token_for_prototype" \
      -d '{
        "role": "user",
        "content": "My favorite color is blue."
      }' \
      http://127.0.0.1:9004/memory/add
    ```
    

#### 2. Retrieve Recent Memories

- **Endpoint:** `GET /memory/retrieve/recent`
    
- **Description:** Retrieves the most recently added memories.
    
- **Query Parameters:**
    
    - `last` (optional): An integer specifying the number of most recent memories to retrieve. If omitted, all short-term memories will be returned.
        
- **Example `curl` commands:**
    
    - **Get the last 5 memories:**
        
        ```
        curl -X GET \
          -H "X-Auth: super_secret_token_for_prototype" \
          "http://127.0.0.1:9004/memory/retrieve/recent?last=5"
        ```
        
    - **Get all recent memories:**
        
        ```
        curl -X GET \
          -H "X-Auth: super_secret_token_for_prototype" \
          "http://127.0.0.1:9004/memory/retrieve/recent"
        ```
        

#### 3. Retrieve Semantic Memories

- **Endpoint:** `GET /memory/retrieve/semantic`
    
- **Description:** Performs a semantic search based on a query and returns the most relevant memories.
    
- **Query Parameters:**
    
    - `query` (required): The text query for semantic search.
        
    - `k` (optional): An integer specifying the number of top relevant memories to retrieve (defaults to 5).
        
- **Example `curl` command:**
    
    ```
    curl -X GET \
      -H "X-Auth: super_secret_token_for_prototype" \
      "http://127.0.0.1:9004/memory/retrieve/semantic?query=what%20is%20my%20dog's%20name&k=1"
    ```
    
    **Note:** Remember to URL-encode your query string (e.g., spaces become `%20`).
    

## Persistence

The server automatically saves its state to disk:

- `memory_data.json`: Stores the raw text content of your memories.
    
- `memory_index.faiss`: Stores the Faiss vector index in a binary format.
    

These files are loaded automatically when the server starts, ensuring your memory is persistent across sessions.



## Future Improvements

- **More Robust Error Handling:** Implement more granular error codes and messages for API responses.
    
- **Configuration File:** Externalize paths, port, and authentication token into a configuration file.
        
- **Batch Embedding:** Utilize `llama-embedding`'s batch processing capability for adding multiple memories at once.
        
- **Memory Deletion/Update:** Add API endpoints for deleting or updating existing memory entries.
    
- **Dockerization:** Provide a Dockerfile for easy deployment and dependency management.
    
- **Client Libraries:** Develop simple client libraries in Python or other languages for easier integration.

## Contributing

Contributions are welcome! Please fork the repository, make your changes on a new branch, and open a pull request. Report bugs or suggest features by opening an issue.