#include "ngx_c_http.h"
#include <iostream>

HttpServer::HTTP_CODE HttpServer::Connection_func(char* text)
{
    text += strspn(text, " \t");
    if(strncasecmp(text, "keep-alive", 11) == 0)
    {
        m_keeplive = true;
    }
    else if(strncasecmp(text, "close", 5) == 0)
    {
        m_keeplive = false;
    }

    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Host_func(char* text)
{
    m_host = text + strspn(text, " \t");

    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Connect_Length_func(char* text)
{
    text += strspn(text, " \t");
    m_content_length = std::stoi(text);
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Range_func(char* text)
{
    text += strspn(text, " \t");

    if(strncasecmp(text, "bytes=", 6) == 0)
    {
        text += 6;
        char* temp = strchr(text, '-');
        if(temp != nullptr)
        {
            *temp++ = '\0';
            m_range_start = std::stoi(text);
            m_range_end = std::stoi(temp);
        }
        m_header_func_vec.emplace_back(std::bind(&HttpServer::range, this));
    } 
    else
    {
        return BAD_REQUEST;
    }  
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::If_UnModified_Since_func(char* text)
{
    text += strspn(text, " \t");
    m_if_modified_since_time = strtime_convert_time_t(text); 
    return NO_REQUEST;    
}

HttpServer::HTTP_CODE HttpServer::If_Modified_Since_func(char* text)
{
    text += strspn(text, " \t");
    m_if_modified_since_time = strtime_convert_time_t(text); 
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Accept_func(char* text)
{
    text += strspn(text, " \t");
    decompose_weight(accept_map, text);
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Accept_Charset_func(char* text)
{
    text += strspn(text, " \t");
    decompose_weight(accept_charset_map, text);
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Accept_Encoding_func(char* text)
{
    text += strspn(text, " \t");
    decompose_weight(accept_encoding_map, text);
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Accept_Language_func(char* text)
{
    text += strspn(text, " \t");
    decompose_weight(accept_language_map, text);
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Cookie_func(char* text)
{
    text += strspn(text, " \t");
    m_cookie_value = strchr(text, '=');
    *m_cookie_value++ = '\0';
    m_cookie_name = text;

    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::Authorization_func(char* text)
{
    text += strspn(text, " \t");
    return NO_REQUEST;
}

HttpServer::HTTP_CODE HttpServer::If_Match_func(char* text)
{
    text += strspn(text, " \t");
    char* temp1 = strchr(text, '"');
    *temp1++ = '\0';
    char* temp2 = strchr(temp1, '"');
    *temp2++ = '\0';
    m_ETag = temp1;

    return NO_REQUEST;
}