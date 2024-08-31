#include "httpServer.h"
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void httpServer::sendToClient(int client_fd, const char *msg, int msg_size)
{
    send(client_fd, msg, msg_size, 0);
}

void httpServer::onAccept()
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);  
    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (client_fd < 0)
    {
        std::cerr << "Failed to accept connection\n";
        return;
    }
    epoll_event event_cfg {};
    event_cfg.data.fd = client_fd;
    event_cfg.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event_cfg) < 0)
    {
        std::cerr << "Failed to add client socket to epoll\n";
        close(client_fd);
        return;
    }    
}

void httpServer::removeConnection(int client_fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL) < 0)
    {
        std::cerr << "Failed to remove client socket from epoll\n";
    }
    close(client_fd);
}

void httpServer::onClientConnect(int client_fd)
{
    const int MAX_BUFFER_SIZE = 1024;
    char buffer[MAX_BUFFER_SIZE];
    int msg_size = recv(client_fd, buffer, MAX_BUFFER_SIZE, 0);
    if (msg_size < 0)
    {
        std::cerr << "Failed to read msg from connection.\n";
        removeConnection(client_fd);
    }
    else if (msg_size == 0)
    {
        std::cout << "Removing connection\n";
        removeConnection(client_fd);
    }
    else
    {
        onMessageRecieved(client_fd, buffer, msg_size);
    }
}

void httpServer::onClientDisconnect(int client_fd)
{
    // Optionally handle client disconnection logic here
}

void httpServer::onMessageRecieved(int client_fd, const char *msg, int msg_size)
{
    httpRequest request(msg, msg_size);
    if (request.method == httpMethod::GET)
    {
        onGet(client_fd, request);
    }
}

void httpServer::onGet(int client_fd, httpRequest &request)
{
    httpResponse response;
    if (request.url.starts_with("/echo/"))
    {
        response = httpResponse::_200();
        const std::string& echo = request.url.substr(6);
        response.body = echo;
        response.headers["Content-Type"] = "text/plain";
        response.headers["Content-Length"] = std::to_string(echo.size());
    }
    else if (request.url == "/user-agent" && request.headers.count("User-Agent") == 1)
    {
        response = httpResponse::_200();
        const std::string& user_agent = request.headers.at("User-Agent");
        response.body = user_agent;
        response.headers["Content-Type"] = "text/plain";
        response.headers["Content-Length"] = std::to_string(user_agent.size());
    }
    else if (request.url == "/")
    {
        response = httpResponse::_200();
        response.body = "Hello, World!";
        response.headers["Content-Type"] = "text/plain";
        response.headers["Content-Length"] = std::to_string(response.body.size());
    }
    else
    {
        response = httpResponse::_404();
    }
    std::string response_str = httpResponse::to_string(response);
    sendToClient(client_fd, response_str.c_str(), response_str.size());
}

httpServer::httpServer(const char *address, int port, int backlog) 
    : server_fd{-1}, epoll_fd{-1}, port{port}, backlog{backlog}
{
    // Use the address as it is, for now using INADDR_ANY
    this->address = address ? inet_addr(address) : INADDR_ANY;
}

httpServer::~httpServer()
{
    close(server_fd);
    close(epoll_fd);
}

// Returns 0 if no errors occurred, -1 if an error occurred
int httpServer::init()
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return -1;
    }
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) 
    {
        std::cerr << "setsockopt failed\n";
        return -1;
    }  
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = address;
    server_addr.sin_port = htons(port);
  
    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) 
    {
        std::cerr << "Failed to bind to port " << port << '\n';
        return -1;
    }
    if (listen(server_fd, backlog) != 0) 
    {
        std::cerr << "listen failed\n";
        return -1;
    }
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        std::cerr << "epoll_create1 failed\n";
        return -1;
    }
    epoll_event event_cfg;
    event_cfg.data.fd = server_fd;
    event_cfg.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event_cfg) < 0)
    {
        std::cerr << "Failed to add server_fd to epoll\n";
        return -1;
    }
    std::cout << "Server is now online\n";
    return 0;
}

void httpServer::run()
{
    std::cout << "Server is now running\n";
    const int MAX_EVENTS = 100;
    epoll_event events[MAX_EVENTS];
    while (true)
    {
        int events_size = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (events_size < 0)
        {
            std::cerr << "epoll_wait failed\n";
            continue;
        }
        for (int i = 0; i < events_size; ++i)
        {
            int event_fd = events[i].data.fd;
            if (event_fd == server_fd)
            {
                std::cout << "New connection coming in.\n";
                onAccept();
            }
            else
            {
                std::cout << "Request coming in.\n";
                onClientConnect(event_fd);
            }
        }
    }
}
