#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <sstream>

// Function to split a message based on a delimiter
std::vector<std::string> split_message(const std::string &message, const std::string& delim) {
    std::vector<std::string> toks;
    std::stringstream ss = std::stringstream{message};
    std::string line;
    while (getline(ss, line, *delim.begin())) {
        toks.push_back(line);
        ss.ignore(delim.length() - 1);
    }
    return toks;
}

// Function to get the request path from the HTTP request
std::string get_path(const std::string &request) {
    std::vector<std::string> toks = split_message(request, "\r\n");
    std::vector<std::string> path_toks = split_message(toks[0], " ");
    return path_toks[1];
}

// Function to extract the User-Agent header from the request
std::string get_user_agent(const std::string &request) {
    std::vector<std::string> lines = split_message(request, "\r\n");
    for (const auto &line : lines) {
        if (line.find("User-Agent:") == 0) {
            return line.substr(strlen("User-Agent:"));
        }
    }
    return ""; // Return empty if User-Agent is not found
}

int main(int argc, char **argv) {
    std::cout << "Logs from your program will appear here!\n";
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    // Set socket options to avoid 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    std::cout << "Waiting for a client to connect...\n";

    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if (client_fd < 0) {
        std::cerr << "Error in accepting client" << std::endl;
    } else {
        std::cout << "Client connected\n";
    }

    char buffer[1024];
    int ret = read(client_fd, buffer, sizeof(buffer));
    if (ret < 0) {
        std::cerr << "Error in reading from client socket" << std::endl;
    } else if (ret == 0) {
        std::cout << "No bytes read" << std::endl;
    } else {
        std::string request(buffer);
        std::cout << "Request: " << request << std::endl;

        std::string path = get_path(request);
        std::string user_agent = get_user_agent(request);
        std::vector<std::string> split_paths = split_message(path, "/");
        
        std::string response;
        if (path == "/") {
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\nHello, World!";
        } else if (path == "/user-agent") {
            // Respond with the User-Agent
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(user_agent.length()) + "\r\n\r\n" + user_agent;
        } else if (split_paths[1] == "echo") {
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(split_paths[2].length()) + "\r\n\r\n" + split_paths[2];
        } else {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }

        std::cout << "Response: " << response << std::endl;
        write(client_fd, response.c_str(), response.length());
    }

    close(client_fd);
    close(server_fd);
    return 0;
}