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

        // Extract User-Agent value
        std::string user_agent = "";
        size_t user_agent_pos = request.find("User-Agent: ");
        if (user_agent_pos != std::string::npos) {
            size_t user_agent_end = request.find("\r\n", user_agent_pos);
            if (user_agent_end != std::string::npos) {
                // Correctly extract the User-Agent string
                user_agent = request.substr(user_agent_pos + 12, user_agent_end - (user_agent_pos + 12));
            }
        }

        // Prepare HTTP response
        std::string response_body = user_agent;
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " 
                               + std::to_string(response_body.size()) + "\r\n\r\n" + response_body;

        send(client, response.c_str(), response.size(), 0);
        close(client);
    }

    close(server_fd);
    return 0;
}
