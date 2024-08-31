#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <unordered_map>

// Enum for HTTP methods
enum class httpMethod {
    GET,
    POST,
    // Add other methods as needed
};

// Simple HTTP request structure
struct httpRequest {
    httpMethod method;
    std::string url;
    std::unordered_map<std::string, std::string> headers;

    // Constructor to parse the HTTP request
    httpRequest(const char *msg, int msg_size) {
        // Basic parsing logic (you may want to improve this)
        std::string request(msg, msg_size);
        size_t method_end = request.find(' ');
        std::string method_str = request.substr(0, method_end);
        
        if (method_str == "GET") {
            method = httpMethod::GET;
        } else {
            method = httpMethod::POST; // Default to POST for simplicity
        }

        size_t url_start = method_end + 1;
        size_t url_end = request.find(' ', url_start);
        url = request.substr(url_start, url_end - url_start);
        
        // Parse headers (basic implementation)
        size_t header_start = request.find("\r\n") + 2;
        size_t header_end = request.find("\r\n\r\n");
        std::string header_section = request.substr(header_start, header_end - header_start);
        size_t pos = 0;
        while ((pos = header_section.find("\r\n")) != std::string::npos) {
            std::string line = header_section.substr(0, pos);
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 2); // Skip ': '
                headers[key] = value;
            }
            header_section.erase(0, pos + 2);
        }
    }
};

// Simple HTTP response structure
struct httpResponse {
    int status_code;
    std::string body;
    std::unordered_map<std::string, std::string> headers;

    // Static methods to create responses
    static httpResponse _200() {
        return {200, "", {}};
    }

    static httpResponse _404() {
        return {404, "404 Not Found", {}};
    }

    static httpResponse _400() {
        return {400, "400 Bad Request", {}};
    }

    // Convert response to string
    static std::string to_string(const httpResponse &response) {
        std::string response_str = "HTTP/1.1 " + std::to_string(response.status_code) + " OK\r\n";
        for (const auto &header : response.headers) {
            response_str += header.first + ": " + header.second + "\r\n";
        }
        response_str += "\r\n" + response.body;
        return response_str;
    }
};

class httpServer {
public:
    httpServer(const char *address, int port, int backlog);
    ~httpServer();
    int init();
    void run();

private:
    void sendToClient(int client_fd, const char *msg, int msg_size);
    void onAccept();
    void removeConnection(int client_fd);
    void onClientConnect(int client_fd);
    void onClientDisconnect(int client_fd);
    void onMessageRecieved(int client_fd, const char *msg, int msg_size);
    void onGet(int client_fd, httpRequest &request);

    int server_fd;
    int epoll_fd;
    in_addr_t address;
    int port;
    int backlog;
};

httpServer::httpServer(const char *address, int port, int backlog) 
    : server_fd{-1}, epoll_fd{-1}, port{port}, backlog{backlog} {
    if (address == nullptr || std::string(address).empty()) {
        this->address = INADDR_ANY; // Default to INADDR_ANY
    } else {
        this->address = inet_addr(address); // Convert string address to binary form
    }
}

httpServer::~httpServer() {
    close(server_fd);
    close(epoll_fd);
}

void httpServer::sendToClient(int client_fd, const char *msg, int msg_size) {
    send(client_fd, msg, msg_size, 0);
}

void httpServer::onAccept() {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (client_fd < 0) {
        std::cerr << "Failed to accept connection\n";
        return;
    }
    epoll_event event_cfg {};
    event_cfg.data.fd = client_fd;
    event_cfg.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event_cfg) < 0) {
        std::cerr << "Failed to add client socket to epoll\n";
        close(client_fd);
        return;
    }
}

void httpServer::removeConnection(int client_fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr) < 0) {
        std::cerr << "Failed to remove client socket from epoll\n";
    }
    close(client_fd);
}

void httpServer::onClientConnect(int client_fd) {
    const int MAX_BUFFER_SIZE = 1024;
    char buffer[MAX_BUFFER_SIZE];
    int msg_size = recv(client_fd, buffer, MAX_BUFFER_SIZE, 0);
    if (msg_size < 0) {
        std::cerr << "Failed to read msg from connection.\n";
        removeConnection(client_fd);
    } else if (msg_size == 0) {
        std::cout << "Removing connection\n";
        removeConnection(client_fd);
    } else {
        onMessageRecieved(client_fd, buffer, msg_size);
    }
}

void httpServer::onMessageRecieved(int client_fd, const char *msg, int msg_size) {
    httpRequest request(msg, msg_size);
    if (request.method == httpMethod::GET) {
        onGet(client_fd, request);
    }
}

void httpServer::onGet(int client_fd, httpRequest &request) {
    httpResponse response;
    if (request.url.starts_with("/echo/")) {
        response = httpResponse::_200();
        const std::string& echo = request.url.substr(6);
        response.body = echo;
        response.headers["Content-Type"] = "text/plain";
        response.headers["Content-Length"] = std::to_string(echo.size());
    } else if (request.url == "/user-agent") {
        if (request.headers.count("User-Agent") > 0) {
            response = httpResponse::_200();
            const std::string& user_agent = request.headers["User-Agent"];
            response.body = user_agent;
            response.headers["Content-Type"] = "text/plain";
            response.headers["Content-Length"] = std::to_string(user_agent.size());
        } else {
            response = httpResponse::_400(); // Bad Request if User-Agent is missing
        }
    } else if (request.url == "/") {
        response = httpResponse::_200();
        response.body = "Welcome to the HTTP Server!";
        response.headers["Content-Type"] = "text/plain";
        response.headers["Content-Length"] = std::to_string(response.body.size());
    } else {
        response = httpResponse::_404();
    }
    std::string response_str = httpResponse::to_string(response);
    sendToClient(client_fd, response_str.c_str(), response_str.size());
}

int httpServer::init() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return -1;
    }
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return -1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = address;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind to port " << port << '\n';
        return -1;
    }
    if (listen(server_fd, backlog) != 0) {
        std::cerr << "listen failed\n";
        return -1;
    }
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "epoll_create1 failed\n";
        return -1;
    }
    epoll_event event_cfg;
    event_cfg.data.fd = server_fd;
    event_cfg.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event_cfg) < 0) {
        std::cerr << "Failed to add server_fd to epoll\n";
        return -1;
    }
    std::cout << "Server is now online\n";
    return 0;
}

void httpServer::run() {
    std::cout << "Server is now running\n";
    const int MAX_EVENTS = 100;
    epoll_event events[MAX_EVENTS];
    while (true) {
        int events_size = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (events_size < 0) {
            std::cerr << "epoll_wait failed\n";
            continue;
        }
        for (int i = 0; i < events_size; ++i) {
            int event_fd = events[i].data.fd;
            if (event_fd == server_fd) {
                std::cout << "New connection coming in.\n";
                onAccept();
            } else {
                std::cout << "Request coming in.\n";
                onClientConnect(event_fd);
            }
        }
    }
}

int main() {
    httpServer server("127.0.0.1", 8080, 10);
    if (server.init() == 0) {
        server.run();
    }
    return 0;
}