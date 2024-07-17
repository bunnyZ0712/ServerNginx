#include <cstring>
#include <string>
#include <iostream>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/uio.h>
#include "ngx_c_http.h"

bool HttpServer::Business(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength, bool& deleteFlag)
{
    char* tptr = reinterpret_cast<char*>(pMsgHeader);
    // LPSTRUC_MSG_HEADER pMsgHeader = reinterpret_cast<LPSTRUC_MSG_HEADER>(tptr); //消息头
    LPCOMM_PKG_HEADER  pPkgHeader = reinterpret_cast<LPCOMM_PKG_HEADER>(tptr + MSGHEADERLEN);//包头
    deleteFlag = false;             //是否释放发送缓冲区
    m_read_buffer = pPkgBody; //整个消息由消息头+包头+数据构成,取到数据部分
    m_read_idx = iBodyLength;
    // std::cout << m_read_buffer << std::endl;
    HTTP_CODE ret = process_read();
    process_write(ret);
    pPkgHeader ->pkgLen =  strlen(m_write_buffer) + m_content_length + PKGHEADERLEN;    //填充包头内容
    pConn ->isendlen = pPkgHeader ->pkgLen;   //要发送的数据长度
    m_iov[0].iov_base = pPkgHeader;         //填充发送包头的长度和指针
    m_iov[0].iov_len = PKGHEADERLEN; 
    // HttpWriteData(pMsgHeader ->pConn ->fd, m_iov, m_iov_count, 0);

    return true;
}

int HttpServer::ReadData(int fd, void* dataBuf, size_t length, int flag)
{
    return recv(fd, dataBuf, length, flag);
}

int HttpServer::WriteData(int fd, const void* dataBuf, size_t iovCount, int flag)
{
    return HttpWriteData(fd, m_iov, m_iov_count, flag);
}

// const char* HttpServer::GetWriteBuf(void)
// {
//     return &m_write_buffer[0];
// }

HttpServer::HttpServer()
{
    init();
    parse_header_func_map.emplace("Connection", std::bind(&HttpServer::Connection_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Host", std::bind(&HttpServer::Host_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Connect-Length", std::bind(&HttpServer::Connect_Length_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Range", std::bind(&HttpServer::Range_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("If-Modified-Since", std::bind(&HttpServer::If_Modified_Since_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Accept", std::bind(&HttpServer::Accept_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Accept-Charset", std::bind(&HttpServer::Accept_Charset_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Accept-Encoding", std::bind(&HttpServer::Accept_Encoding_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Accept-Language", std::bind(&HttpServer::Accept_Language_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("Cookie", std::bind(&HttpServer::Cookie_func, this, std::placeholders::_1));
    parse_header_func_map.emplace("If-Match", std::bind(&HttpServer::If_Match_func, this, std::placeholders::_1));

    parse_method_func_map.emplace(GET, std::bind(&HttpServer::GET_func, this, std::placeholders::_1));
    parse_method_func_map.emplace(POST, std::bind(&HttpServer::POST_func, this, std::placeholders::_1));
    parse_method_func_map.emplace(HEAD, std::bind(&HttpServer::HEAD_func, this, std::placeholders::_1));
    parse_method_func_map.emplace(OPTIONS, std::bind(&HttpServer::OPTIONS_func, this, std::placeholders::_1));
}

void HttpServer::init(void)
{
    m_start_idx = 0;
    m_check_idx = 0;
    m_read_idx = 0;  
    m_write_idx = 0; 
    m_iov_count = 0; 
    m_method = GET;
    m_file_ptr = nullptr;
    m_url = nullptr;        //请求URL
    m_version = nullptr;    //HTTP版本
    m_local_fileaddr = nullptr; //默认文件夹
    m_cookie_value = nullptr;
    m_cookie_name = nullptr;
    bool m_keeplive = true;        //是否保持长连接,HTTP1.0默认关闭，HTTP1.1默认打开
    m_range_start = 0;      //范围获取开始
    m_range_end = 0;        //范围获取结束
    m_host = nullptr;           //host
    m_ETag = nullptr;
    m_content_length = 0;   //主体长度
    m_if_modified_since_time = 0;
    m_if_unmodified_since_time = 0;
    m_check_state = CHECK_STATE_REQUETLINE;
    // bzero(m_read_buffer, READ_BUFFER_SIZE);
    bzero(m_write_buffer, WRITE_BUFFER_SIZE);
    bzero(m_filename, FILENAME_LEN);
    bzero(m_time_buffer, TIME_BUFFER_SIZE);
}

int HttpServer::HttpWriteData(int fd, const void* dataBuf, size_t iovCount, int flag)
{
    void* tData = const_cast<void*>(dataBuf);
    iovec* writeBuf = static_cast<iovec*>(tData);
    auto ret = writev(fd, writeBuf, iovCount);
    if(m_file_ptr != nullptr)
    {
        munmap(m_file_ptr, m_stat.st_size);
    }
    return ret;
}

HttpServer::LINE_STATUS HttpServer::parse_line(char* text)
{
    char temp;
    for(; m_check_idx < m_read_idx; m_check_idx++)
    {
        temp = text[m_check_idx];
        if(text[m_check_idx] == '\r')
        {
            if(m_check_idx +1 == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if(text[m_check_idx + 1] == '\n')
            {
                text[m_check_idx++] = '\0';
                text[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(text[m_check_idx] == '\n')
        {
            if(m_check_idx > 1 && text[m_check_idx - 1] == '\r')
            {
                text[m_check_idx++] = '\0';
                text[m_check_idx++] = '\0';
                return LINE_OK;                
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

HttpServer::HTTP_CODE HttpServer::parse_request(char* text)
{
    m_url = strpbrk(text, " \t");
    if(m_url == nullptr)
    {
        return BAD_REQUEST;
    }

    *m_url++ = '\0';

    if(strcasecmp(text, "GET") == 0)    //检查方法
    {
        m_method = GET;
    }
    else if(strcasecmp(text, "POST") == 0)
    {
        m_method = POST;
    }
    else if(strcasecmp(text, "HEAD") == 0)
    {
        m_method = HEAD;
    }
    else if(strcasecmp(text, "OPTIONS") == 0)
    {
        m_method = OPTIONS;
    }
    
    m_url += strspn(m_url, " \t");  //字符串中各字符连续出现的个数
    
    m_version = strpbrk(m_url, " \t");  //寻找字符串中字符出现的第一个位置
    if(m_version == nullptr)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url, "http://", 7) != 0)
    {
        return BAD_REQUEST;
    }
    else
    {
        m_url += 7;
        m_url = strchr(m_url, '/'); //寻找该字符第一次出现的位置
    }

    if(m_url == nullptr || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;    
}

HttpServer::HTTP_CODE HttpServer::parse_header(char* text)  //解析头部信息
{
    if(text[0] == '\0')
    {
        if(m_content_length > 0)
        {
            m_check_state = CHECK_STATE_CONTENT;

            return NO_REQUEST;
        }
        return GET_REQUEST;
    }

    char* temp = strchr(text, ':'); //找到:位置并置\0，用来在容器中寻找对应处理函数，可避免使用ifelse判断
    *temp++ = '\0';

    auto it = parse_header_func_map.find(text); 
    if(it == parse_header_func_map.end())
    {
        return BAD_REQUEST;
    }

    return it ->second(temp);   //temp已经处理到分号：以后了
}

HttpServer::HTTP_CODE HttpServer::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_check_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;    
}

HttpServer::HTTP_CODE HttpServer::process_read(void)
{
    LINE_STATUS line_status;
    HTTP_CODE ret;
    char* text;
    while(true)
    {
        line_status = parse_line(m_read_buffer);
        if(line_status == LINE_OK && m_check_state == CHECK_STATE_CONTENT || line_status == LINE_OK)
        {
            text = &m_read_buffer[m_start_idx]; //读取到完整行，供解析使用
            m_start_idx = m_check_idx;  //跳到下一行起始位置
            switch(m_check_state)
            {
                case CHECK_STATE_REQUETLINE:
                {
                    ret = parse_request(text);
                    if(ret == BAD_REQUEST)
                    {
                        return BAD_REQUEST;
                    }
                    break;
                }
                case CHECK_STATE_HEADER:
                {
                    ret = parse_header(text);
                    if(ret == BAD_REQUEST)
                    {
                        return BAD_REQUEST;
                    }
                    else if(ret == GET_REQUEST)
                    {
                        return do_request();
                    }
                    break;
                }
                case CHECK_STATE_CONTENT:
                {
                    ret = parse_content(text);
                    if(ret == BAD_REQUEST)
                    {
                        return BAD_REQUEST;
                    }
                    else if(ret == GET_REQUEST)
                    {
                        return do_request();
                    }
                    break;
                }
                default:
                    return INTERNAL_ERROR;
            }
        }
        else    //LINE_BAD读取行错误  LINE_OPEN按道理读取完会返回LINE_OK，如果最后不是\r\n结尾就会导致这种结果
        {
            return BAD_REQUEST;
        }
    }
}

HttpServer::HTTP_CODE HttpServer::do_request(void)
{
    if(m_method == OPTIONS)
    {
        return FILE_REQUEST;
    }
    int ret;
    int fd;
    memcpy(m_filename, LOCAL_FILE_ADDR, 10);
    memcpy(&m_filename[10], m_url, strlen(m_url));
    
    ret = stat(m_filename, &m_stat);
    if(ret == -1)
    {
        return NO_RESOURCE;
    }
    if(S_ISDIR(m_stat.st_mode)) //是否为文件夹
    {
        return BAD_REQUEST;
    }
    if(!(S_IROTH & m_stat.st_mode))
    {
        return FORBIDDEN_REQUEST;
    }

    if(m_if_modified_since_time != 0)   //处理if-modified-since
    {
        if(m_stat.st_mtime > m_if_modified_since_time)  //如果发生改变(纳秒没进行比较)
        {
            m_header_func_vec.emplace_back(std::bind(&HttpServer::if_modified_since_func, this));
        }
        else
        {
            return NOT_MODIFIED;
        }
    }
    else if(m_if_unmodified_since_time != 0)
    {
        if(m_stat.st_mtime > m_if_modified_since_time)
        {
            return PRECONDITION_FAILED;
        }
    }

    fd = open(m_filename, O_RDONLY);
    if(fd == -1)
    {
        return INTERNAL_ERROR;
    }
    m_file_ptr = (char*)mmap(nullptr, m_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if(m_file_ptr == MAP_FAILED)
    {
        return INTERNAL_ERROR;
    }
    m_content_length = m_stat.st_size;
    if((m_range_start != 0 && m_range_end !=0) && !(m_range_start < 0 || m_range_end > m_stat.st_size))
    {
        m_content_length = m_range_end - m_range_start + 1;
        return PARTIAL_CONTENT;
    }

    return FILE_REQUEST;
}

template<>
void HttpServer::write_senddata<const char*>(const char* data)
{
    memcpy(&m_write_buffer[m_write_idx] , data, strlen(data));
    m_write_idx += strlen(data);    
}

template<>
void HttpServer::write_senddata<char*>(char* data)
{
    memcpy(&m_write_buffer[m_write_idx] , data, strlen(data));
    m_write_idx += strlen(data);    
}

void HttpServer::add_status_line(int status, const char* title) //添加响应行
{
    add_senddata("HTTP/1.1 ", status, " ",title, "\r\n");
}

void HttpServer::add_blank_line(void)
{
    add_senddata("\r\n");
}

void HttpServer::add_date(void)
{
    char tmvalue[TIME_BUFFER_SIZE] = {0};
    time_t_convert_strtime(time(NULL), tmvalue);

    add_senddata("Date: ",tmvalue, "\r\n");
}

void HttpServer::add_content(const char* text)  //出现错误添加的主体信息
{
    add_senddata(text);
}

void HttpServer::add_headers(const char* text)
{
    add_senddata("Connect-Length: ", strlen(text), "\r\n");
    add_blank_line();
}

void HttpServer::add_cookie(const char* cookieName, time_t expirse, const char* path, const char* domain)
{
    char tmbuf[TIME_BUFFER_SIZE] = {0};
    time_t_convert_strtime(time(NULL) + expirse, tmbuf);
    add_senddata("Set-Cookie: ");
    add_senddata(cookieName, '=',get_cookie_value(const_cast<char*>("zyf")).c_str(), "; ");
    add_senddata("expirse=", tmbuf, "; ");
    add_senddata("path=", path, "; ");
    add_senddata("domain=", domain, "\r\n");
}

void HttpServer::process_write(HttpServer::HTTP_CODE http_code)
{
    auto it = parse_method_func_map.find(m_method);

    it ->second(http_code);
}

