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
    std::stringstream ss(message);
    std::string line;
    while (getline(ss, line, *delim.begin())) {
        toks.push_back(line);
        ss.ignore(delim.length() - 1);
    }
    return toks;
}

std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

std::string get_method(const std::string &request) {
    std::vector<std::string> toks = split_message(request, "\r\n");
    std::vector<std::string> path_toks = split_message(toks[0], " ");
    return path_toks[0];
}

// Function to get the request path from the HTTP request
std::string get_path(const std::string &request) {
    std::vector<std::string> toks = split_message(request, "\r\n");
    std::vector<std::string> path_toks = split_message(toks[0], " ");
    return path_toks[1];
}

size_t get_content_length(const std::string &request) {
    std::vector<std::string> lines = split_message(request, "\r\n");
    for (const auto &line : lines) {
        if (line.find("Content-Length:") == 0) {
            return std::stoull(trim(line.substr(strlen("Content-Length:"))));
        }
    }
    return 0;
}

std::string get_request_body(const std::string &request) {
    std::vector<std::string> lines = split_message(request, "\r\n");
    int body_start = -1;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) {
            body_start = i + 1;
            break;
        }
    }
    if (body_start != -1) {
        return request.substr(request.find("\r\n\r\n") + 4);
    }
    return "";
}

// Function to extract the User-Agent header from the request
std::string get_user_agent(const std::string &request) {
    std::vector<std::string> lines = split_message(request, "\r\n");
    for (const auto &line : lines) {
        if (line.find("User-Agent:") == 0) {
            return trim(line.substr(strlen("User-Agent:")));
        }
    }
    return ""; // Return empty if User-Agent is not found
}

// Function to extract the Accept-Encoding header from the request
std::string get_accept_encoding(const std::string &request) {
    std::vector<std::string> lines = split_message(request, "\r\n");
    for (const auto &line : lines) {
        if (line.find("Accept-Encoding:") == 0) {
            return trim(line.substr(strlen("Accept-Encoding:")));
        }
    }
    return ""; // Return empty if Accept-Encoding is not found
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
        std::string accept_encoding = get_accept_encoding(request);
        std::string method = get_method(request);

        std::vector<std::string> split_paths = split_message(path, "/");
        std::string response;

        bool gzip_supported = accept_encoding.find("gzip") != std::string::npos;

        if (method == "GET") {
            if (path == "/") {
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\nHello, World!";
            } else if (path == "/user-agent") {
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(user_agent.length()) + "\r\n\r\n" + user_agent;
            } else if (split_paths.size() > 1 && split_paths[1] == "echo") {
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(split_paths[2].length()) + "\r\n\r\n" + split_paths[2];
            } else if (split_paths.size() > 1 && split_paths[1] == "files") {
                std::string filename = split_paths[2];
                fs::path file_path = fs::path(directory) / filename;
                if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
                    std::ifstream file(file_path, std::ios::binary);
                    if (file) {
                        file.seekg(0, std::ios::end);
                        std::streamsize size = file.tellg();
                        file.seekg(0, std::ios::beg);
                        std::string file_content(size, '\0');
                        if (file.read(&file_content[0], size)) {
                            response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(size) + "\r\n\r\n" + file_content;
                        }
                    }
                } else {
                    response = "HTTP/1.1 404 Not Found\r\n\r\n";
                }
            } else {
                response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }

            if (gzip_supported) {
                // Add Content-Encoding header if gzip is supported
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Encoding: gzip\r\n" + response.substr(response.find("\r\n\r\n"));
            }

        } else if (method == "POST") {
            if (split_paths.size() > 1 && split_paths[1] == "files") {
                std::string filename = split_paths[2];
                fs::path file_path = fs::path(directory) / filename;
                std::string request_body = get_request_body(request);
                size_t content_length = get_content_length(request);
                std::ofstream file(file_path, std::ios::binary);
                if (file) {
                    file.write(request_body.c_str(), content_length);
                    response = "HTTP/1.1 201 Created\r\n\r\n";
                } else {
                    response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                }
            } else {
                response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }
        } else {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }

        std::cout << "Response: " << response << std::endl;
        write(client_fd, response.c_str(), response.length());
    }
    close(client_fd);
}

int main(int argc, char **argv) {
    std::string directory = "."; // Default directory
    if (argc > 2 && std::string(argv[1]) == "--directory") {
        directory = argv[2];
    }

    std::cout << "Logs from your program will appear here!\n";
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);
    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr))
