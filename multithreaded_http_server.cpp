#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080
#define ROOT_DIR "./static"
#define THREAD_POOL_SIZE 4
#define LOG_FILE "server.log"

// Thread pool and synchronization
std::queue<int> taskQueue;
std::mutex queueMutex;
std::condition_variable condition;
bool serverRunning = true;

// Logging
std::mutex logMutex;

// Function to write logs
void writeLog(const std::string &message)
{
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream logFile(LOG_FILE, std::ios::app);
    if (logFile.is_open())
    {
        logFile << message << std::endl;
    }
}

// Function to get the current timestamp
std::string getCurrentTimestamp()
{
    std::time_t now = std::time(nullptr);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;
}

// Function to send HTTP responses
void sendResponse(int clientSocket, const std::string &content, const std::string &contentType, int statusCode = 200)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode;

    if (statusCode == 200)
    {
        response << " OK\r\n";
    }
    else if (statusCode == 404)
    {
        response << " Not Found\r\n";
    }
    else
    {
        response << " Internal Server Error\r\n";
    }

    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << content.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << content;

    std::string responseStr = response.str();
    send(clientSocket, responseStr.c_str(), responseStr.size(), 0);
}

// Helper function to check if a string ends with a given suffix
bool endsWith(const std::string &str, const std::string &suffix)
{
    if (str.size() < suffix.size())
    {
        return false;
    }
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Function to determine the content type based on file extension
std::string getContentType(const std::string &filePath)
{
    if (endsWith(filePath, ".html"))
    {
        return "text/html";
    }
    else if (endsWith(filePath, ".css"))
    {
        return "text/css";
    }
    else if (endsWith(filePath, ".js"))
    {
        return "application/javascript";
    }
    else if (endsWith(filePath, ".jpg") || endsWith(filePath, ".jpeg"))
    {
        return "image/jpeg";
    }
    else if (endsWith(filePath, ".png"))
    {
        return "image/png";
    }
    else
    {
        return "text/plain";
    }
}

// Function to handle client requests
void handleRequest(int clientSocket, const std::string &clientIP)
{
    char buffer[2048] = {0};
    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0)
    {
        close(clientSocket);
        return;
    }

    // Parse HTTP request
    std::istringstream requestStream(buffer);
    std::string requestLine;
    std::getline(requestStream, requestLine);

    std::istringstream lineStream(requestLine);
    std::string method, url, version;
    lineStream >> method >> url >> version;

    if (method != "GET")
    {
        writeLog(getCurrentTimestamp() + " [" + clientIP + "] Unsupported method: " + method);
        sendResponse(clientSocket, "Method Not Allowed", "text/plain", 405);
        close(clientSocket);
        return;
    }

    // Prevent directory traversal
    if (url.find("..") != std::string::npos)
    {
        writeLog(getCurrentTimestamp() + " [" + clientIP + "] Directory traversal attempt: " + url);
        sendResponse(clientSocket, "Bad Request", "text/plain", 400);
        close(clientSocket);
        return;
    }

    // Default to index.html
    if (url == "/")
    {
        url = "/index.html";
    }

    std::string filePath = ROOT_DIR + url;
    if (!std::filesystem::exists(filePath))
    {
        writeLog(getCurrentTimestamp() + " [" + clientIP + "] File not found: " + url);
        sendResponse(clientSocket, "404 Not Found", "text/plain", 404);
        close(clientSocket);
        return;
    }

    // Read file content
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        writeLog(getCurrentTimestamp() + " [" + clientIP + "] Failed to open file: " + url);
        sendResponse(clientSocket, "Internal Server Error", "text/plain", 500);
        close(clientSocket);
        return;
    }

    std::ostringstream contentStream;
    contentStream << file.rdbuf();
    std::string content = contentStream.str();

    std::string contentType = getContentType(filePath);
    sendResponse(clientSocket, content, contentType, 200);

    writeLog(getCurrentTimestamp() + " [" + clientIP + "] Served: " + url);

    close(clientSocket);
}

// Worker thread function
void workerThread()
{
    while (serverRunning)
    {
        int clientSocket;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, []
                           { return !taskQueue.empty() || !serverRunning; });

            if (!serverRunning && taskQueue.empty())
            {
                return;
            }

            clientSocket = taskQueue.front();
            taskQueue.pop();
        }

        // Get client IP address
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        getpeername(clientSocket, (struct sockaddr *)&clientAddr, &addrLen);
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

        handleRequest(clientSocket, clientIP);
    }
}

int main()
{
    int serverSocket;
    struct sockaddr_in serverAddr;

    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind socket
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Bind failed");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(serverSocket, 10) < 0)
    {
        perror("Listen failed");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    std::cout << "Server is running on port " << PORT << std::endl;

    // Start worker threads
    std::vector<std::thread> threadPool;
    for (int i = 0; i < THREAD_POOL_SIZE; ++i)
    {
        threadPool.emplace_back(workerThread);
    }

    // Main server loop to accept connections
    while (serverRunning)
    {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &addrLen);
        if (clientSocket < 0)
        {
            perror("Accept failed");
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            taskQueue.push(clientSocket);
        }
        condition.notify_one();
    }

    // Clean up
    for (auto &thread : threadPool)
    {
        thread.join();
    }

    close(serverSocket);
    return 0;
}