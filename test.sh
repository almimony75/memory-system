#!/bin/bash

# Configuration
SERVER_URL="http://localhost:9004"
AUTH_TOKEN="super_secret_token_for_prototype"

# Helper functions
function add_memory() {
    local role="$1"
    local content="$2"
    curl -s -X POST \
        -H "Content-Type: application/json" \
        -H "X-Auth: $AUTH_TOKEN" \
        -d "{\"role\":\"$role\",\"content\":\"$content\"}" \
        "$SERVER_URL/memory/add"
}

function semantic_search() {
    local query="$1"
    local k="${2:-5}"
    local encoded_query=$(echo "$query" | jq -sRr @uri)
    echo "Testing query: '$query' (k=$k)"
    curl -s -H "X-Auth: $AUTH_TOKEN" \
        "$SERVER_URL/memory/retrieve/semantic?query=$encoded_query&k=$k" | jq .
    echo ""
}

# Clear previous test data (optional)
rm -f memory_index.faiss memory_data.json

# Wait for server to start
echo "Waiting for server to initialize..."
sleep 3

# Add diverse set of test memories
echo "Adding test memories..."
add_memory "user" "I have a black cat named Shadow"
add_memory "assistant" "Black cats are beautiful animals"
add_memory "user" "My dog is a golden retriever"
add_memory "assistant" "Golden retrievers are friendly dogs"
add_memory "user" "I enjoy birdwatching on weekends"
add_memory "assistant" "Birdwatching is a relaxing hobby"
add_memory "user" "Shadow knocked over my coffee this morning"
add_memory "user" "I need to buy more coffee beans"
add_memory "assistant" "Colombian coffee beans are excellent"
add_memory "user" "My favorite programming language is Python"
add_memory "assistant" "Python is great for machine learning"
add_memory "user" "I'm learning about neural networks"
add_memory "assistant" "Neural networks are inspired by biological brains"
add_memory "user" "The brain contains billions of neurons"
add_memory "user" "I drink coffee while programming in Python"
add_memory "assistant" "Coffee and coding is a common combination"

# Wait for embeddings to process
sleep 2

# Test 1: Exact matches
echo "=== Testing exact matches ==="
semantic_search "cat"
semantic_search "dog"
semantic_search "birdwatching"

# Test 2: Related concepts
echo "=== Testing related concepts ==="
semantic_search "pet"          # Should return cat and dog memories
semantic_search "animal"       # Should return pet-related and bird memories
semantic_search "programming"  # Should return Python and coding memories

# Test 3: Synonym testing
echo "=== Testing synonyms ==="
semantic_search "java"         # Should return programming-related memories (related to Python)
semantic_search "feline"       # Should return cat memories
semantic_search "canine"       # Should return dog memories

# Test 4: Phrase queries
echo "=== Testing phrase queries ==="
semantic_search "black cat"
semantic_search "golden retriever"
semantic_search "coffee beans"

# Test 5: Conceptual similarity
echo "=== Testing conceptual similarity ==="
semantic_search "AI"           # Should return machine learning/neural network memories
semantic_search "caffeine"     # Should return coffee-related memories
semantic_search "hobby"        # Should return birdwatching memory

# Test 6: Multiple results ranking
echo "=== Testing result ranking ==="
semantic_search "coffee" 5     # Should rank "coffee while programming" higher than "buy coffee beans"

# Test 7: Edge cases
echo "=== Testing edge cases ==="
semantic_search ""             # Empty query
semantic_search " "            # Space query
semantic_search "asdfghjkl"    # Nonsense query
semantic_search "猫"           # Non-ASCII characters (cat in Japanese)

echo "Semantic search tests completed!"