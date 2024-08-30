#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

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
        int client = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client < 0) {
            std::cerr << "accept failed\n";
            return 1;
        }

        char buffer[1024] = {0};
        read(client, buffer, sizeof(buffer) - 1);

        std::string request(buffer);
        std::cout << "Received request: " << request << std::endl;

        // Extract the HTTP method and path
        std::string method, path;
        size_t method_end = request.find(' ');
        if (method_end != std::string::npos) {
            method = request.substr(0, method_end);
            size_t path_start = method_end + 1;
            size_t path_end = request.find(' ', path_start);
            if (path_end != std::string::npos) {
                path = request.substr(path_start, path_end - path_start);
            }
        }

        std::string response;
        if (method == "GET" && path == "/") {
            // Valid path
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!";
        } else {
            // Invalid path
            response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n404 Not Found";
        }

        send(client, response.c_str(), response.length(), 0);
        close(client);
    }

    close(server_fd);
    return 0;
}
