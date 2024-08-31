#include <iostream>
#include <string>
#include <unordered_map>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

// Assuming you have some utility functions and classes
namespace httpResponse {
    std::string _200() {
        // Generates the base response for HTTP 200 OK
        return "HTTP/1.1 200 OK\r\n";
    }
}

void sendToClient(int client_fd, const char* data, size_t size) {
    send(client_fd, data, size, 0);
}

int main(int argc, char **argv) {
    std::cout << std::unitbuf; // Flush after every std::cout / std::cerr
    std::cerr << std::unitbuf;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    std::cout << "Waiting for a client to connect...\n";

    while (true) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "accept failed\n";
            return 1;
        }

        char buffer[1024] = {0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read < 0) {
            std::cerr << "read failed\n";
            close(client_fd);
            continue;
        }

        buffer[bytes_read] = '\0'; // Null-terminate the string
        std::string request(buffer);
        std::cout << "Received request: " << request << std::endl;

        // Extract the HTTP method, URL, and headers
        std::string method, url;
        std::unordered_map<std::string, std::string> headers;

        size_t method_end = request.find(' ');
        if (method_end != std::string::npos) {
            method = request.substr(0, method_end);
            size_t url_start = method_end + 1;
            size_t url_end = request.find(' ', url_start);
            if (url_end != std::string::npos) {
                url = request.substr(url_start, url_end - url_start);
            }

            // Extract headers
            size_t headers_start = request.find("\r\n") + 2;
            size_t headers_end = request.find("\r\n\r\n", headers_start);
            if (headers_end != std::string::npos) {
                std::string header_section = request.substr(headers_start, headers_end - headers_start);
                size_t pos = 0;
                while ((pos = header_section.find("\r\n")) != std::string::npos) {
                    std::string line = header_section.substr(0, pos);
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos) {
                        std::string key = line.substr(0, colon_pos);
                        std::string value = line.substr(colon_pos + 2); // Skip ": "
                        headers[key] = value;
                    }
                    header_section.erase(0, pos + 2);
                }
            }
        }

        // Prepare response
        std::string response;
        if (method == "GET" && url.starts_with("/echo/")) {
            response = httpResponse::_200();
            const std::string& echo = url.substr(6);
            response += "Content-Type: text/plain\r\n";
            response += "Content-Length: " + std::to_string(echo.size()) + "\r\n\r\n";
            response += echo;
        } else if (method == "GET" && url == "/user-agent" && headers.count("User-Agent")) {
            response = httpResponse::_200();
            const std::string& user_agent = headers.at("User-Agent");
            response += "Content-Type: text/plain\r\n";
            response += "Content-Length: " + std::to_string(user_agent.size()) + "\r\n\r\n";
            response += user_agent;
        } else {
            // Invalid path or method
            response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\n404 Not Found";
        }

        std::cout << "Response: " << response << std::endl;

        sendToClient(client_fd, response.c_str(), response.size());
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
