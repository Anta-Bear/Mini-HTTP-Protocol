#ifndef __TASK_HPP
#define __TASK_HPP

#include <iostream>
#include <functional>

#include "Protocol.hpp"

class Task
{
public:
    Task() 
    {}
    Task(int _sock) : sock(_sock)
    {}
    ~Task() 
    {}
    
    // 处理任务
    void ProcessOn()
    {
        handler(sock);
    }

private:
    int sock;
    CallBack handler;
};

#endif