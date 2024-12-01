# Multithreaded Web Server | Operating Systems 

A simple multithreaded web server implemented in C++ using basic OS concepts.

## Features
- Serves static files (HTML, CSS, images).
- Multithreaded request handling using a thread pool.
- Thread-safe logging.
- Basic error handling for unsupported methods and missing files.

## How to Run
1. Compile the code:
   ```bash
   g++ -std=c++17 -pthread simple_web_server.cpp -o simple_web_server
