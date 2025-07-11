# C++ Memory System Server

**A lightweight C++ server designed to manage a conversational agent's memory, providing a simple API for adding and retrieving memory entries. It features in-memory storage with a fixed size, background persistence to disk, and basic API key authentication.**


## Features

* **Fixed-Size Memory Buffer:** Stores the `N` most recent memory entries in a `std::deque`.
* **Disk Persistence:** Automatically flushes memory entries to `memory_history.json` in the background.
* **Simple REST API:** Exposes endpoints for adding and retrieving memory.
* **Basic Authentication:** Requires an `X-Auth` header for all API calls.
* **Timestamping:** Automatically adds UTC timestamps to each memory entry.
* **Multithreaded:** Utilizes Crow's multithreading capabilities and a dedicated writer thread for persistence.

## Prerequisites

**To build and run this server, you will need:**

* **C++ Compiler:** A C++17 compatible compiler (e.g., GCC, Clang, MSVC).
* **Crow C++ Microframework:** For the web server functionalities.
* **nlohmann/json:** A header-only C++ JSON library.

## API Endpoints

**The server exposes two main API endpoints. All requests must include the **`X-Auth` header for authentication.

### Authentication

**All API requests must include a header: **`X-Auth: super_secret_token_for_prototype`

**Note:** For a production environment, this token should be stored securely (e.g., environment variable) and a more robust authentication mechanism should be implemented.

### `POST /memory/add`

**Adds a new memory entry to the system.**

* **URL:** `/memory/add`
* **Method:** `POST`
* **Headers:**

  * `Content-Type: application/json`
  * `X-Auth: super_secret_token_for_prototype`
* **Request Body (JSON):**

  ```
  {
    "role": "user",
    "content": "What is the capital of France?"
  }

  ```
* **Success Response (200 OK):**

  ```
  {
    "status": "success",
    "message": "Memory entry added"
  }

  ```
* **Error Responses:**

  * `400 Bad Request`: If the JSON body is invalid or missing
    ```
    {
      "status": "error",
      "message": "Invalid request body: 'role' and 'content' required"
    }

    ```
  * `401 Unauthorized`: If the `X-Auth` header is missing or incorrect.
    ```
    {
      "status": "error",
      "message": "unauthorized"
    }

    ```
* **Example using `curl`:**

  ```
  curl -X POST \
       -H "Content-Type: application/json" \
       -H "X-Auth: super_secret_token_for_prototype" \
       -d '{"role": "user", "content": "Hello, how are you?"}' \
       http://localhost:5073/memory/add

  ```

### `GET /memory/retrieve`

**Retrieves memory entries from the system.**

* **URL:** `/memory/retrieve`
* **Method:** `GET`
* **Headers:**
  * `X-Auth: super_secret_token_for_prototype`
* **Query Parameters:**
  * `last` (optional, integer): The number of most recent memory entries to retrieve. If omitted, all available entries (up to `max_entries`) will be returned.
* **Success Response (200 OK):** Returns a JSON array of `MemoryEntry` objects.
  ```
  [
    {
      "timestamp": "2023-10-27T10:00:00Z",
      "role": "system",
      "content": "System initialized."
    },
    {
      "timestamp": "2023-10-27T10:01:00Z",
      "role": "user",
      "content": "What is the capital of France?"
    },
    {
      "timestamp": "2023-10-27T10:01:30Z",
      "role": "assistant",
      "content": "The capital of France is Paris."
    }
  ]

  ```
* **Error Responses:**
  * `400 Bad Request`: If the `last` parameter is not a valid integer or is negative.
    ```
    {
      "status": "error",
      "message": "Invalid 'last' parameter: must be an integer"
    }

    ```
  * `401 Unauthorized`: If the `X-Auth` header is missing or incorrect.
* **Example using `curl` (retrieve all):**
  ```
  curl -X GET \
       -H "X-Auth: super_secret_token_for_prototype" \
       http://localhost:5073/memory/retrieve

  ```
* **Example using `curl` (retrieve last 2):**
  ```
  curl -X GET \
       -H "X-Auth: super_secret_token_for_prototype" \
       "http://localhost:5073/memory/retrieve?last=2"

  ```

## Memory Persistence

**The **`MemoryManager` automatically saves the entire `memory_` deque to a file named `memory_history.json`. This happens when:

* **The number of unsaved entries reaches ** `flush_every_` (default: 100).
* **A maximum **`flush_interval_ms_` (default: 1000ms or 1 second) has passed since the last flush, and there are unsaved entries.

**Upon server startup, the **`MemoryManager` attempts to load existing memory from `memory_history.json`, ensuring continuity across server restarts.

## Configuration

**The **`MemoryManager` can be configured with the following parameters in its constructor:

```
MemoryManager(size_t max_entries = 500, size_t flush_every = 100, int flush_interval_ms = 1000)

```

* `max_entries`: The maximum number of memory entries to keep in the deque (default: 500). When this limit is exceeded, the oldest entry is removed.
* `flush_every`: The number of new entries after which a disk flush is triggered (default: 100).
* `flush_interval_ms`: The maximum time (in milliseconds) between disk flushes, even if `flush_every` is not reached (default: 1000).

**You can modify these values in **`main.cpp`:

```
// In main()
MemoryManager mem(1000, 50, 500); // Example: 1000 max, flush every 50, or every 500ms

```

## Contributing

**Feel free to fork the repository, open issues, or submit pull requests.**
