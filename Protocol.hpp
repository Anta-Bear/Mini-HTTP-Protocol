#ifndef __PROTOCOL_HPP
#define __PROTOCOL_HPP

#include <sys/types.h>
#include <vector>
#include <unordered_map>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <sys/sendfile.h>
#include <fcntl.h>

#include "TcpServer.hpp"
#include "Util.hpp"
#include "Log.hpp"

#define SEP ": "
#define WEB_ROOT "wwwroot"
#define HOME_PAGE "index.html"
#define HTTP_VERSION "HTTP/1.0"
#define LINE_END "\r\n"
#define PAGE_404 "404.html"

// Error code
#define OK 200
#define NOT_FOUND 404
#define BAD_REQUEST 400
#define SERVER_ERROR 500

// 状态码 => 状态码描述
static std::string Code2Desc(int code)
{
    std::string desc;
    switch (code)
    {
    case 200:
        desc = "OK";
        break;
    case 404:
        desc = "Not Found";
        break;
    default:
        break;
    }
    return desc;
}

static std::string SuffixToDesc(std::string &suffix)
{
    // 后缀 => 描述
    static std::unordered_map<std::string, std::string> suffixtodesc = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".jpg", "application/x-jpg"}};

    auto iter = suffixtodesc.find(suffix);
    if (iter != suffixtodesc.end())
    {
        return iter->second;
    }

    return "text/html";
}

class HttpRequest
{
public:
    std::string request_line;
    std::vector<std::string> request_header;
    std::string blank;
    std::string request_body;

    // 解析之后的结果
    std::string method;
    std::string uri; // path?args
    std::string version;

    // header 解析之后的结果
    std::unordered_map<std::string, std::string> header_kv;
    int content_length;
    std::string path;
    std::string suffix; // 后缀
    std::string query_string;

    bool cgi;
    int size;

public:
    HttpRequest(int contentlength = 0) : content_length(contentlength), cgi(false)
    {}
    ~HttpRequest()
    {}
};

class HttpResponse
{
public:
    std::string status_line;
    std::vector<std::string> response_header;
    std::string blank;
    std::string response_body;

    int status_code;
    int fd;

public:
    HttpResponse(int statuscode = OK) : blank(LINE_END), status_code(statuscode), fd(-1)
    {
    }
    ~HttpResponse()
    {
    }
};

// 读取请求，分析请求，构建相应
class EndPoint
{
private:
    int sock;
    HttpRequest http_request;
    HttpResponse http_response;
    bool stop;

private:
    bool RecvHttpRequestLine()
    {
        auto &line = http_request.request_line;
        if (Util::ReadLine(sock, line) > 0)
        {
            line.resize(line.size() - 1);
            LOG(INFO, line);
        }
        else
        {
            stop = true;
        }
        return stop;
    }

    bool RecvHttpRequestHeader()
    {
        std::string line;
        while (true)
        {
            line.clear();
            if (Util::ReadLine(sock, line) <= 0)
            {
                stop = true;
                break;
            }

            if (line == "\n")
            {
                http_request.blank = line;
                break;
            }

            line.resize(line.size() - 1); // 去掉 \n
            http_request.request_header.push_back(line);
            LOG(INFO, line);
        }

        return stop;
    }

    void ParseHttpRequestLine()
    {
        auto &line = http_request.request_line;
        std::stringstream ss(line);
        ss >> http_request.method >> http_request.uri >> http_request.version;
        // 全部转为大写
        auto &method = http_request.method;
        std::transform(method.begin(), method.end(), method.begin(), ::toupper);
    }

    void ParseHttpRequestHeader()
    {
        std::string key;
        std::string value;
        for (auto &iter : http_request.request_header)
        {
            if (Util::CutString(iter, key, value, SEP))
            {
                http_request.header_kv.insert({key, value});
            }
        }
    }

    bool IsNeedRecvHttpRequestBody()
    {
        auto &method = http_request.method;
        if (method == "POST")
        {
            auto &header_kv = http_request.header_kv;
            auto iter = header_kv.find("Content-Length");
            if (iter != header_kv.end())
            {
                http_request.content_length = atoi(iter->second.c_str());
                return true;
            }
        }

        return false;
    }

    bool RecvHttpRequestBody()
    {
        if (IsNeedRecvHttpRequestBody())
        {
            int content_length = http_request.content_length;
            auto &body = http_request.request_body;
            char ch = 0;
            while (content_length)
            {
                ssize_t s = recv(sock, &ch, 1, 0);
                if (s > 0)
                {
                    body.push_back(ch);
                    content_length--;
                }
                else
                {
                    stop = true;
                    break;
                }
            }
        }

        return stop;
    }

    int ProcessNonCgi()
    {
        http_response.fd = open(http_request.path.c_str(), O_RDONLY);
        if (http_response.fd >= 0)
        {
            return OK;
        }

        return NOT_FOUND;
    }

    int ProcessCgi()
    {
        // 新线程走到这里，只有httpserver一个线程，所以不能直接调用exec
        // 创建子进程
        int code = OK;
        auto &bin = http_request.path;
        auto &method = http_request.method;
        auto &query_string = http_request.query_string; // GET
        auto &request_body = http_request.request_body; // POST
        auto &response_body = http_response.response_body;

        int content_length = http_request.content_length;
        std::string query_string_env;
        std::string method_env;
        std::string content_length_env;

        // 站在父进程角度
        int input[2];
        int output[2];
        if (pipe(input) < 0)
        {
            LOG(ERROR, "pipe input error");
            code = SERVER_ERROR;
            return code;
        }
        if (pipe(output) < 0)
        {
            LOG(ERROR, "pipe output error");
            code = SERVER_ERROR;
            return code;
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            // child
            close(input[0]);
            close(output[1]);

            // 进行重定向
            // input[1]  写出
            // output[0] 读入
            dup2(input[1], 1);
            dup2(output[0], 0);

            method_env = "METHOD=";
            method_env += method;
            putenv((char *)method_env.c_str());

            if (method == "GET")
            {
                query_string_env = "QUERY_STRING=";
                query_string_env += query_string;
                putenv((char *)query_string_env.c_str());
            }
            else if (method == "POST")
            {
                content_length_env = "Content_Length= ";
                content_length_env += std::to_string(content_length);
                putenv((char *)content_length_env.c_str());
            }
            else
            {
                // Do nothing
            }

            // exec --> 进程程序替换，不需要知道0/1，只需要知道读0，写1即可
            execl(bin.c_str(), bin.c_str(), nullptr);
            exit(1);
        }
        else if (pid < 0)
        {
            // ERROR
            LOG(ERROR, "fork error!");
            return 404;
        }
        else
        {
            // parent
            close(input[1]);
            close(output[0]);

            if (method == "POST")
            {
                const char *start = request_body.c_str();
                int total = 0;
                int size = 0;
                while ((total < content_length) && (size = write(output[1], start + total, request_body.size() - total)) > 0)
                {
                    total += size;
                }
            }

            char ch;
            while (read(input[0], &ch, 1) > 0)
            {
                response_body.push_back(ch);
            }

            int status = 0;
            pid_t ret = waitpid(pid, &status, 0);
            if (ret == pid)
            {
                if (WIFEXITED(status))
                {
                    if (WEXITSTATUS(status) == 0)
                    {
                        code = OK;
                    }
                    else
                    {
                        code = BAD_REQUEST;
                    }
                }
                else
                {
                    code = SERVER_ERROR;
                }
            }

            close(input[0]);
            close(output[1]);
        }
        return code;
    }

    void BuildOkResponse()
    {
        std::string line = "Content-Type: ";
        line += SuffixToDesc(http_request.suffix);
        line += LINE_END;
        http_response.response_header.push_back(line);
        line = "Content-Length: ";
        line += std::to_string(http_request.size);
        line += LINE_END;
        http_response.response_header.push_back(line);
    }

    void HandlerError(std::string page)
    {
        http_request.cgi = false; // ?
        http_response.fd = open(page.c_str(), O_RDONLY);
        if (http_response.fd > 0)
        {
            struct stat st;
            stat(page.c_str(), &st);
            http_request.size = st.st_size;
            std::string line = "Content-Type: text/html";
            line += LINE_END;
            http_response.response_header.push_back(line);
            line = "Content-Length: ";
            if (http_request.cgi)
            {
                line += std::to_string(http_response.response_body.size());
            }
            else
            {
                // GET
                line += std::to_string(st.st_size);
            }
            line += LINE_END;
            http_response.response_header.push_back(line);
        }
    }

    void BuildHttpResponseHelper()
    {
        auto &code = http_response.status_code;

        // 构建状态行
        auto &status_line = http_response.status_line;
        status_line += HTTP_VERSION;
        status_line += " ";
        status_line += std::to_string(code);
        status_line += " ";
        status_line += Code2Desc(code);
        status_line += LINE_END;

        // 构建相应正文，可能包括响应报头
        std::string path = WEB_ROOT;
        path += "/";
        switch (code)
        {
        case OK:
            BuildOkResponse();
            break;
        case NOT_FOUND:
            path += PAGE_404;
            HandlerError(path);
            break;
        case BAD_REQUEST:
            path += PAGE_404;
            HandlerError(path);
            break;
        case SERVER_ERROR:
            path += PAGE_404;
            HandlerError(path);
            break;
        default:
            break;
        }
    }

public:
    EndPoint(int _sock) : sock(_sock), stop(false)
    {
    }

    bool IsStop()
    {
        return stop;
    }
    void RecvHttpRequest()
    {
        if (RecvHttpRequestLine() || RecvHttpRequestHeader())
        {
            // recv error!
        }
        else
        {
            ParseHttpRequestLine();
            ParseHttpRequestHeader();
            RecvHttpRequestBody();
        }
    }

    void BuildHttpResponse()
    {
        std::string _path;
        auto &code = http_response.status_code;
        struct stat st;
        int size = 0;
        size_t found = 0; // 查找后缀的坐标
        if (http_request.method != "GET" && http_request.method != "POST")
        {
            LOG(WARRING, "method is not right!");
            code = BAD_REQUEST;
            goto END;
        }
        if (http_request.method == "GET")
        {
            size_t pos = http_request.uri.find('?');
            if (pos != std::string::npos)
            {
                Util::CutString(http_request.uri, http_request.path, http_request.query_string, "?");
                http_request.cgi = true;
            }
            else
            {
                http_request.path = http_request.uri;
            }
        }
        else if (http_request.method == "POST")
        {
            http_request.cgi = true;
            http_request.path = http_request.uri;
        }
        else
        {
            // Do nothing
        }

        _path = http_request.path;
        http_request.path = WEB_ROOT;
        http_request.path += _path;
        if (http_request.path[http_request.path.size() - 1] == '/')
        {
            http_request.path += HOME_PAGE;
        }

        if (stat(http_request.path.c_str(), &st) == 0)
        {
            // 资源是存在的
            if (S_ISDIR(st.st_mode))
            {
                // 请求的是一个目录，是不被允许的
                http_request.path += "/";
                http_request.path += HOME_PAGE;
                // 重新获取一下
                stat(http_request.path.c_str(), &st);
            }
            if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            {
                // 是可执行文件，需要特殊处理
                http_request.cgi = true;
            }

            // 目标文件的大小
            // size = st.st_size;
            http_request.size = st.st_size;
        }
        else
        {
            // 资源不存在
            std::string info = http_request.path;
            info += "  Not found";
            LOG(WARRING, info);
            http_response.status_code = NOT_FOUND;
            goto END;
        }

        // 提取 path 的后缀
        found = _path.rfind(".");
        if (found == std::string::npos)
        {
            http_request.suffix = ".html";
        }
        else
        {
            http_request.suffix = _path.substr(found);
        }

        if (http_request.cgi)
        {
            code = ProcessCgi();
        }
        else
        {
            // 简单的网页返回，返回静态网页
            code = ProcessNonCgi();
        }
    END:
        BuildHttpResponseHelper();
    }

    void SendHttpResponse()
    {
        send(sock, http_response.status_line.c_str(), http_response.status_line.size(), 0);
        for (auto &iter : http_response.response_header)
        {
            send(sock, iter.c_str(), iter.size(), 0);
        }
        send(sock, http_response.blank.c_str(), http_response.blank.size(), 0);

        // std::cout << http_request.path << "-----" << std::endl;
        if (http_request.cgi)
        {
            // 在 body 中
            auto &response_body = http_response.response_body;
            int size = 0;
            int total = 0;
            const char *start = response_body.c_str();
            while ((total < response_body.size()) && send(sock, start + total, response_body.size() - total, 0) > 0)
            {
                total += size;
            }
        }
        else
        {
            sendfile(sock, http_response.fd, nullptr, http_request.size);
            close(http_response.fd);
        }
    }

    ~EndPoint()
    {
        close(sock);
    }
};

class CallBack
{
public:
    CallBack() 
    {}

    ~CallBack()
    {}

    // 仿函数
    void operator()(int sock)
    {
        HandlerRequest(sock);
    }

    void HandlerRequest(int sock)
    {
        LOG(INFO, "hander request begin ...");
#ifdef DEBUG
        // for test
        char buffer[4096];
        recv(sock, buffer, sizeof(buffer), 0);
        std::cout << "-----------begin--------------" << std::endl;
        std::cout << buffer << std::endl;
        std::cout << "-----------end----------------" << std::endl;
#else
        EndPoint *ep = new EndPoint(sock);
        ep->RecvHttpRequest();
        if (!ep->IsStop())
        {
            LOG(INFO, "Recv no error, Begin Build And Send");
            ep->BuildHttpResponse();
            ep->SendHttpResponse();
        }
        else
        {
            LOG(WARRING, "Recv error, Stop Build And Send"); 
        }
        delete ep;
#endif
        LOG(INFO, "handler request end ...");

    }
};

#endif