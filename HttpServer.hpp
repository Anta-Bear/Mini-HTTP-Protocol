#ifndef __HTTP_SERVER_HPP
#define __HTTP_SERVER_HPP

#include <pthread.h>
#include <signal.h>

#include "TcpServer.hpp"
#include "Log.hpp"
#include "Task.hpp"
#include "ThreadPool.hpp"

#define DEAFULT_PORT 8081

class HttpServer
{
private:
    int _port;
    bool _stop;

public:
    HttpServer(int port = DEAFULT_PORT) : _port(port), _stop(false)
    {
    }

    void InitServer()
    {
        // SIGPIPE 信号需要忽略，如果不忽略，在写入的时候，可能导致 server 直接崩溃
        signal(SIGPIPE, SIG_IGN);
        // _tcp_Server = TcpServer::getInstance(_port);
    }

    // 直接把 accept 上来的链接交给后端的线程池处理
    void Loop()
    {
        TcpServer *tsvr = TcpServer::getInstance(_port);
        ThreadPool *tp = ThreadPool::GetInstance();
        LOG(INFO, "loop begin...");
        while (!_stop)
        {
            struct sockaddr_in peer;
            socklen_t len = sizeof(peer);
            int sock = accept(tsvr->Sock(), (struct sockaddr *)&peer, &len);
            if (sock < 0) continue;

            LOG(INFO, "Get a new link ...");
            Task task(sock);
            tp->PushTask(task);
        }
    }
};

#endif
