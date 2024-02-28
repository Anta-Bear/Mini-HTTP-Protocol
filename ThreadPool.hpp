#pragma once

#include <iostream>
#include <queue>
#include <pthread.h>

#include "Task.hpp"
#include "Log.hpp"

#define NUM 10

class ThreadPool
{
private:
    ThreadPool(int _num = NUM) : num(_num), stop(false)
    {
        pthread_mutex_init(&lock, nullptr);
        pthread_cond_init(&cond, nullptr);
    }

    ~ThreadPool()
    {
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
    }

    ThreadPool(const ThreadPool& tp) = delete;

    static ThreadPool* single_instance;

public:

    static ThreadPool* GetInstance()
    {
        static pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;
        if (single_instance == nullptr) {
            pthread_mutex_lock(&mymutex);
            if (single_instance == nullptr) {
                single_instance = new ThreadPool();
                single_instance->InitThreadPool();
            }
            pthread_mutex_unlock(&mymutex);
        }

        return single_instance;
    }

    bool IsStop()
    {
        return stop;
    }

    void ThreadWait()
    {
        pthread_cond_wait(&cond, &lock);

    }

    void ThreadWakeUp()
    {
        pthread_cond_signal(&cond);
    }

    void Lock()
    {
        pthread_mutex_lock(&lock);
    }

    void UnLock()
    {
        pthread_mutex_unlock(&lock);
    }

    bool IsEmpty()
    {
        return task_queue.size() == 0 ? true : false;
    }

    // 线程执行函数
    static void* ThreadRoutine(void* args)
    {
        ThreadPool* tp = (ThreadPool*)args;
        while (true)
        {
            Task task;
            tp->Lock();
            while (tp->IsEmpty())
            {
                tp->ThreadWait();
            }

            // 满足条件
            tp->PopTask(task);
            tp->UnLock();
            
            // 处理任务
            task.ProcessOn();
        }
    }

    bool InitThreadPool()
    {
        for (int i = 0; i < num; i ++)
        {
            pthread_t tid;
            if (pthread_create(&tid, nullptr, ThreadRoutine, this) != 0) {
                LOG(FATAL, "create thread pool error");
                return false;
            }
        }

        LOG(INFO, "create thread pool success");
        return true;
    }

    void PushTask(const Task& task)
    {
        Lock();
        task_queue.push(task);
        UnLock();
        ThreadWakeUp();
    }

    void PopTask(Task& task)
    {
        task = task_queue.front();
        task_queue.pop();
    }

private:
    std::queue<Task> task_queue;
    int num; // 线程的数量
    bool stop;
    pthread_mutex_t lock;
    pthread_cond_t cond; 
};

ThreadPool* ThreadPool::single_instance = nullptr;