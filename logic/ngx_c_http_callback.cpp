#include "ngx_c_http.h"

void HttpServer::if_modified_since_func(void)
{
    time_t_convert_strtime(m_stat.st_mtime, m_time_buffer);
    add_senddata("Last-Modified: ", m_time_buffer, "\r\n");
}

void HttpServer::range(void)
{
    if(!(m_range_start < 0 || m_range_end > m_stat.st_size))    //可以处理范围请求
    {
        add_senddata("Accept: bytes ", m_range_start, "-", m_range_end, "\\", m_range_end - m_range_start + 1,"\r\n");
    }   
    else
    {
        m_range_start = 0;
        m_range_end = m_stat.st_size;
        add_senddata("Accept: none\r\n");
    } 
}