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
#include <zlib.h>
#include <fstream>
#include <sys/stat.h>
#include <filesystem> // C++17 for filesystem support
#include <algorithm> 
 // For std::find
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
std::string trim(const std::string &str) {
    const auto first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    const auto last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
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
std::string compress_gzip(const std::string &data) {
    // Initialize the z_stream
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    // The windowBits parameter is set to 15 + 16 to produce a gzip header + trailer around the compressed data
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        std::cerr << "deflateInit2 failed" << std::endl;
        return "";
    }

    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());

    int ret;
    char outbuffer[32768];
    std::string compressed_data;

    // Compress the data in a loop
    do {
        zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        ret = deflate(&zs, Z_FINISH);

        if (compressed_data.size() < zs.total_out) {
            compressed_data.append(outbuffer, zs.total_out - compressed_data.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        std::cerr << "Compression failed: " << ret << std::endl;
        return "";
    }

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

        // Determine response headers and body
        std::string response;
        std::string content_encoding;
        std::string body;

        // Check if gzip compression is supported by the client
        bool gzip_supported = accept_encoding.find("gzip") != std::string::npos;

        if (method == "GET") {
            if (path == "/") {
                body = "Hello, World!";
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
            } else if (path == "/user-agent") {
                body = user_agent;
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
            } else if (path.find("/echo/") == 0) {
                body = path.substr(6);  // Extract the part after "/echo/"
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
            } else if (path.find("/files/") == 0) {
                std::string filename = path.substr(7);  // Extract the filename
                fs::path file_path = fs::path(directory) / filename;  // Construct the full path
                if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
                    std::ifstream file(file_path, std::ios::binary);
                    if (file) {
                        // Get the file size
                        file.seekg(0, std::ios::end);
                        std::streamsize size = file.tellg();
                        file.seekg(0, std::ios::beg);
                        // Read the file content
                        body.resize(size);
                        file.read(&body[0], size);
                        response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n";
                    } else {
                        response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                    }
                } else {
                    response = "HTTP/1.1 404 Not Found\r\n\r\n";
                }
            } else {
                response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }
        } else if (method == "POST") {
            if (path.find("/files/") == 0) {
                std::string filename = path.substr(7);  // Extract the filename
                fs::path file_path = fs::path(directory) / filename;  // Construct the full path
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

        // Compress the body if gzip is supported
        if (gzip_supported && !body.empty()) {
            std::string compressed_body = compress_gzip(body);
            if (!compressed_body.empty()) {
                body = compressed_body;  // Replace the body with compressed data
                content_encoding = "Content-Encoding: gzip\r\n";
            }
        }

        // Finalize the response with the proper headers
        response += content_encoding + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;

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
