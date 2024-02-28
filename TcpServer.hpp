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
    {
    }
    TcpServer(const TcpServer &TcpServer) = delete;

public:
    // 单例模式
    static TcpServer *getInstance(int port)
    {
        static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
        if (nullptr == svr)
        {
            pthread_mutex_lock(&lock);
            if (nullptr == svr)
            {
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

    // 创建套接字
    void Socket()
    {
        _listensock = socket(AF_INET, SOCK_STREAM, 0);
        if (_listensock < 0)
        {
            LOG(FATAL, "socket error");
            exit(1);
        }

        // 目的：当服务器一端断开连接，与客户端进行四次挥手的过程中，可以进行地址复用
        // 防止服务器有大概 2s 的 TIME_WAIT 的延迟
        // 套接字API级别：LEVEL -> SOL_SOCKET
        // SO_REUSEADDR：允许重用本地地址和端口

        int opt = 1;
        setsockopt(_listensock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        LOG(INFO, "socket success");
    }

    // 绑定
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

    // 让服务器处于监听状态
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
        if (_listensock >= 0)
        {
            close(_listensock);
        }
    }

private:
    int _port;
    int _listensock;
    static TcpServer *svr;
};

TcpServer *TcpServer::svr = nullptr;