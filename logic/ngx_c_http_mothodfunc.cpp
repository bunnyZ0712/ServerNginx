#include "ngx_c_http.h"
#include <iostream>

void HttpServer::GET_func(HttpServer::HTTP_CODE http_code)
{
    switch(http_code)
    {
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(error_400_form);
            add_content(error_400_form);
            break;
        }
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(error_500_form);
            add_content(error_500_form);
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(error_404_form);
            add_content(error_404_form);
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(error_403_form);
            add_content(error_403_form);
            break;            
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            add_date();
            add_senddata("Connect-Length: ", m_content_length, "\r\n");
            if(m_cookie_value == nullptr)
            {
                add_cookie("sid", 300);
            }
            for(auto it : m_header_func_vec)
            {
                it();
            }
            add_blank_line();
            m_iov[1].iov_base = m_write_buffer;
            m_iov[1].iov_len = m_write_idx;
            m_iov[2].iov_base = m_file_ptr;
            m_iov[2].iov_len = m_content_length;
            m_iov_count = 3;
            return;
        }
        case NOT_MODIFIED:
        {
            add_status_line(304, redirect_304_title);
            break;            
        }
        case PRECONDITION_FAILED:
        {
            add_status_line(412, error_412_title);
            add_content(error_412_form);
            break;
        }
        case PARTIAL_CONTENT:       //部分请求
        {
            add_status_line(206, ok_206_title);
            add_date();
            add_senddata("Connect-Length: ", m_range_end - m_range_start + 1, "\r\n");
            for(auto it : m_header_func_vec)
            {
                it();
            }
            add_blank_line();
            m_iov[1].iov_base = m_write_buffer;
            m_iov[1].iov_len = m_write_idx;
            m_iov[2].iov_base = (m_file_ptr + m_range_start);
            m_iov[2].iov_len = m_range_end - m_range_start + 1;
            m_iov_count = 3;
            return;
        }
        default:
            break;
    }
    m_iov[1].iov_base = m_write_buffer;
    m_iov[1].iov_len = m_write_idx;  
    m_iov_count = 2; 
}

void HttpServer::POST_func(HttpServer::HTTP_CODE http_code)
{
 
}

void HttpServer::HEAD_func(HttpServer::HTTP_CODE http_code)
{
    char timebuf[TIME_BUFFER_SIZE] = {0};
    time_t_convert_strtime(m_stat.st_mtime, timebuf);
    add_status_line(200, ok_200_title);
    add_date();  
    add_senddata("Allow: ", "GET, POST, HEAD, OPTIONS", "\r\n");
    add_senddata("Content-Length: ", m_content_length, "\r\n");
    add_senddata("Last-Modified: ", timebuf, "\r\n");
    add_senddata("Server: ", "Test1.0", "\r\n");
    add_senddata("Accept: bytes\r\n");
    add_blank_line();

    m_iov[1].iov_base = m_write_buffer;
    m_iov[1].iov_len = m_write_idx;  
    m_iov_count = 2;    
}

void HttpServer::OPTIONS_func(HttpServer::HTTP_CODE http_code)
{
    add_status_line(200, ok_200_title);
    add_date();
    add_senddata("Allow: ", "GET, POST, HEAD, OPTIONS", "\r\n");  
    add_blank_line();
    m_iov[1].iov_base = m_write_buffer;
    m_iov[1].iov_len = m_write_idx;
    m_iov_count = 2;
}