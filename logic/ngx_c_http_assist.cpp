#include "ngx_c_http.h"
#include <ctime>
#include <string>

//分解权重相关
void HttpServer::decompose_weight(std::map<float, std::string, std::greater<float>> strmap, char* text)
{
    char* left, *right;
    left = text;
    right = text;
    while(true)
    {
        right = strchr(right, ';');
        if(right == nullptr)
        {
            strmap.emplace(1, text);
            break;
        }
        *right++ = '\0';
        right += strspn(right, " \t");
        if(strncasecmp(right, "q=", 2) == 0)
        {
            right +=2;
            right += strspn(right, " \t");
            float t = std::stof(right);
            strmap.emplace(t, left);
            right = strchr(right, ',');
            if(right == nullptr)
            {
                break;
            }
            left = ++right;
        }
    }
}

//时间字符串转time_t
time_t HttpServer::strtime_convert_time_t(const char* timestr)
{
    const char *format = "%a, %d %b %Y %H:%M:%S GMT";
    tm time_components;
    memset(&time_components, 0, sizeof(time_components));

    if(strptime(timestr, format, &time_components) != nullptr) 
    {
        return mktime(&time_components);
    } 
    else
    {
        return 0;
    }  
}

//time_t转时间字符串
void HttpServer::time_t_convert_strtime(time_t tmdata, char* tmbuf)
{
    const char *format = "%a, %d %b %Y %H:%M:%S GMT";
    tm* t = localtime(&tmdata);

    strftime(tmbuf, TIME_BUFFER_SIZE, format, t);
}

std::string HttpServer::get_cookie_value(char* text)
{
    long t = time(NULL);

    return std::to_string(t) + text;
}