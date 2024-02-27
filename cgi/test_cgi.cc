#include <iostream>
#include <unistd.h>
#include <stdlib.h>

bool GetQueryString(std::string query_string)
{
    bool result = false;
    std::string method = getenv("METHOD");
    int content_length;
    if (method == "GET")
    {
        query_string = getenv("QUERY_STRING");
        result = true;
        // std::cerr << "Debug " << query_string << std::endl;
    }
    else if (method == "POST")
    {
        content_length = atoi(getenv("Content_Length"));
        char ch = 0;
        while (content_length)
        {
            read(0, &ch, 1);
            query_string.push_back(ch);
            content_length--;
        }
        result = true;
    }
    else
    {
        result = false;
    }
}

void CutString(std::string &in, std::string &out1, std::string &out2, const std::string &sep)
{
    auto pos = in.find(sep);
    if (pos != std::string::npos)
    {
        out1 = in.substr(0, pos);
        out2 = in.substr(pos + sep.size());
    }
}

int main()
{
    std::string query_string;
    GetQueryString(query_string);
    // a=100&b=200
    std::string str1;
    std::string str2;
    std::string name1, value1;
    std::string name2, value2;
    // 对 query_string 进行切分
    CutString(query_string, str1, str2, "&");
    CutString(str1, name1, value1, "=");
    CutString(str2, name2, value2, "=");

    std::cout << name1 << " : " << value1 << std::endl;
    std::cout << name2 << " : " << value2 << std::endl;

    std::cerr << name1 << " : " << value1 << std::endl;
    std::cerr << name2 << " : " << value2 << std::endl;

    return 0;
}
