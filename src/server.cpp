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
#include <filesystem>
#include <algorithm>
#include <zlib.h> // Include zlib for gzip compression
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

// Function to get the value of a header from the request
std::string get_header_value(const std::string &request, const std::string &header_name) {
    std::vector<std::string> lines = split_message(request, "\r\n");
    for (const auto &line : lines) {
        if (line.find(header_name) == 0) {
            return trim(line.substr(header_name.length()));
        }
    }
    return "";
}

// Function to trim whitespace from both ends of a string
std::string trim(const std::string &str) {
    const auto first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    const auto last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

// Function to get the request path from the HTTP request
std::string get_path(const std::string &request) {
    std::vector<std::string> toks = split_message(request, "\r\n");
    std::vector<std::string> path_toks = split_message(toks[0], " ");
    return path_toks[1];
}

// Function to get the request method from the HTTP request
std::string get_method(const std::string &request) {
    std::vector<std::string> toks = split_message(request, "\r\n");
    std::vector<std::string> path_toks = split_message(toks[0], " ");
    return path_toks[0];
}

// Function to get the content length from the HTTP request
size_t get_content_length(const std::string &request) {
    std::string length_str = get_header_value(request, "Content-Length: ");
    return length_str.empty() ? 0 : std::stoull(length_str);
}

// Function to get the request body from the HTTP request
std::string get_request_body(const std::string &request) {
    std::string delimiter = "\r\n\r\n";
    size_t pos = request.find(delimiter);
    return (pos == std::string::npos) ? "" : request.substr(pos + delimiter.length());
}

// Function to compress data using gzip
std::string gzip_compress(const std::string &data) {
    uLongf compressed_size = compressBound(data.size());
    std::string compressed_data(compressed_size, '\0');
    if (compress(reinterpret_cast<Bytef*>(&compressed_data[0]), &compressed_size,
                 reinterpret_cast<const Bytef*>(data.data()), data.size()) != Z_OK) {
        return ""; // Compression failed
    }
    compressed_data.resize(compressed_size);
    return compressed_data;
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
        std::string user_agent = get_header_value(request, "User-Agent: ");
        std::string method = get_method(request);
        std::string accept_encoding = get_header_value(request, "Accept-Encoding: ");

        // Determine response headers
        std::string response;
        std::string content_encoding;
        std::string response_body;

        // Check if the client accepts gzip compression
        if (accept_encoding.find("gzip") != std::string::npos) {
            content_encoding = "Content-Encoding: gzip\r\n";
        }

        if (method == "GET") {
            if (path == "/") {
                response_body = "Hello, World!";
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n" + content_encoding + "Content-Length: " + std::to_string(response_body.length()) + "\r\n\r\n" + response_body;
            } else if (path == "/user-agent") {
                response_body = user_agent;
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n" + content_encoding + "Content-Length: " + std::to_string(response_body.length()) + "\r\n\r\n" + response_body;
            } else if (path.find("/echo/") == 0) {
                std::string echo_str = path.substr(6);
                if (!content_encoding.empty()) {
                    response_body = gzip_compress(echo_str);
                    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n" + content_encoding + "Content-Length: " + std::to_string(response_body.length()) + "\r\n\r\n";
                } else {
                    response_body = echo_str;
                    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(response_body.length()) + "\r\n\r\n" + response_body;
                }
            } else if (path.find("/files/") == 0) {
                std::string filename = path.substr(7); // Get the filename
                fs::path file_path = fs::path(directory) / filename; // Construct the full path
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
                            if (!content_encoding.empty()) {
                                file_content = gzip_compress(file_content);
                                response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n" + content_encoding + "Content-Length: " + std::to_string(file_content.length()) + "\r\n\r\n";
                            } else {
                                response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(size) + "\r\n\r\n";
                            }
                            response += file_content;
                        }
                    }
                } else {
                    response = "HTTP/1.1 404 Not Found\r\n\r\n";
                }
            } else {
                response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }
        } else if (method == "POST") {
            if (path.find("/files/") == 0) {
                std::string filename = path.substr(7); // Get the filename
                fs::path file_path = fs::path(directory) / filename; // Construct the full path
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
    close(client_fd); // Close the client socket
}

int main(int argc, char **argv) {
    std::string directory = "."
