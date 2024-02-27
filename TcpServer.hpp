#pragma once
#include "Log.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h> 
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACK_LOG 5

class TcpServer
{
private:
    TcpServer(int port) : _port(port), _listensock(-1)
    {}
    TcpServer(const TcpServer& TcpServer) = delete;
public:
    static TcpServer* getInstance(int port)
    {
        static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
        if (nullptr == svr) {
            pthread_mutex_lock(&lock);
            if (nullptr == svr) {
                svr = new TcpServer(port);
                svr->InitServer();
            }
            pthread_mutex_unlock(&lock);
        }

        return svr;
    }

    void InitServer()
    {
        Socket();
        Bind();
        Listen();
        LOG(INFO, "tcp_server init ... success");
    }

    void Socket()
    { 
        _listensock = socket(AF_INET, SOCK_STREAM, 0);
        if (_listensock < 0)
        {
            LOG(FATAL, "socket error");
            exit(1);
        }

        int opt = 1;
        setsockopt(_listensock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        LOG(INFO, "socker success");
    }

    void Bind()
    {
        struct sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_port = htons(_port);
        // 云服务器不能直接绑定公网ipv
        local.sin_addr.s_addr = INADDR_ANY;
        if (bind(_listensock, (struct sockaddr *)&local, sizeof(local)) < 0)
        {
            LOG(FATAL, "bind error");
            exit(2);
        }
        LOG(INFO, "bind socket ... success");
    }

    void Listen()
    {
        if (listen(_listensock, BACK_LOG) < 0)
        {
            LOG(FATAL, "listen error");
            exit(3);
        }
        LOG(INFO, "listen socket ... success");
    }

    int Sock()
    {
        return _listensock;
    }

    ~TcpServer()
    {
        if (_listensock >= 0) {
            close(_listensock);
        }
    }

private:
    int _port;
    int _listensock; 
    static TcpServer* svr;
};

TcpServer* TcpServer::svr = nullptr;  