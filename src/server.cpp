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
#include <thread>
#include <fstream>
#include <sys/stat.h>
#include <filesystem> // C++17 for filesystem support

namespace fs = std::filesystem;

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

// Function to trim whitespace from both ends of a string
std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t");
    size_t last = str.find_last_not_of(" \t");
    return (first == std::string::npos) ? "" : str.substr(first, (last - first + 1));
}

// Function to extract the User-Agent header from the request
std::string get_user_agent(const std::string &request) {
    std::vector<std::string> lines = split_message(request, "\r\n");
    for (const auto &line : lines) {
        if (line.find("User-Agent:") == 0) {
            return trim(line.substr(strlen("User-Agent:"))); // Trim whitespace
        }
    }
    return ""; // Return empty if User-Agent is not found
}

// Function to handle client requests
void handle_client(int client_fd, const std::string &directory) {
    char buffer[1024] = {0};
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
        } else if (split_paths.size() > 1 && split_paths[1] == "echo") {
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(split_paths[2].length()) + "\r\n\r\n" + split_paths[2];
        } else if (split_paths.size() > 1 && split_paths[1] == "files") {
            // Handle file requests
            std::string filename = split_paths[2]; // Get the filename
            fs::path file_path = fs::path(directory) / filename; // Construct the full path

            // Check if the file exists
            if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
                std::ifstream file(file_path, std::ios::binary);
                if (file) {
                    // Get the file size
                    file.seekg(0, std::ios::end);
                    std::streamsize size = file.tellg();
                    file.seekg(0, std::ios::beg);

                    // Read the file content
                    std::string file_content(size, '\0');
                    if (file.read(&file_content[0], size)) {
                        response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(size) + "\r\n\r\n" + file_content;
                    }
                }
            } else {
                // File not found
                response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }
        } else {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }

        std::cout << "Response: " << response << std::endl;
        write(client_fd, response.c_str(), response.length());
    }

    close(client_fd); // Close the client socket
}

int main(int argc, char **argv) {
    std::string directory = "."; // Default directory
    if (argc > 2 && std::string(argv[1]) == "--directory") {
        directory = argv[2]; // Get the directory from command line
    }

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
    socklen_t client_addr_len = sizeof(client_addr);

    std::cout << "Waiting for a client to connect...\n";

    while (true) {
        int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "Error in accepting client" << std::endl;
            continue; // Continue to accept other connections
        }

        std::cout << "Client connected\n";

        // Create a new thread to handle the client request
        std::thread(handle_client, client_fd, directory).detach(); // Detach the thread to allow concurrent handling
    }

    close(server_fd);
    return 0;
}