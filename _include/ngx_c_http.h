#ifndef NGX_C_HTTP_H
#define NGX_C_HTTP_H

#include <sys/stat.h>
#include <type_traits>
#include <string>
#include <cstring>
#include <vector>
#include <functional>
#include <map>

#include "ngx_c_slogic.h"
#include "ngx_c_socket.h"

#define READ_BUFFER_SIZE  4096
#define WRITE_BUFFER_SIZE 4096
#define FILENAME_LEN      200
#define TIME_BUFFER_SIZE  30
#define LOCAL_FILE_ADDR   "./Resource"
#define HTTPPORT          80
#define HTTPSPORT         443

class HttpServer:public CLogicSocket{
public:
    HttpServer();
    //留给外部调用的读写接口
	virtual int ReadData(int fd, void* dataBuf, size_t length, int flag) override;  
	virtual int WriteData(int fd, const void* dataBuf, size_t iovCount, int flag) override;
protected://用到的相关数据
    virtual bool Business(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength, bool& deleteFlag) override;
    enum METHOD{    //支持方法
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH
    };
    enum HTTP_CODE{
        NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, //请求读取不完整、请求读取完整、请求错误
        FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION, NOT_MODIFIED, PARTIAL_CONTENT,
        PRECONDITION_FAILED,RESPONSE_SUCCESSFUL //无资源、无访问权限、文件请求、服务器错误、连接已关闭
    };
    enum LINE_STATUS{
        LINE_OK = 0, LINE_BAD, LINE_OPEN    //读取到完整行、行数据错误、行数据不完整(读取完毕)
    };
    enum CHECK_STATE{
        CHECK_STATE_REQUETLINE = 0, //检查请求行
        CHECK_STATE_HEADER, //检查头部信息
        CHECK_STATE_CONTENT //检查主体内容
    };
    const char* ok_200_title = "OK";
    const char* ok_204_title = "No Content";
    const char* ok_206_title = "Partial Content";
    const char* redirect_301_title = "Moved Permanently";
    const char* redirect_302_title = "Found";
    const char* redirect_303_title = "See Other";
    const char* redirect_304_title = "Not Modified";
    const char* redirect_307_title = "Temporary Redirect";
    const char* error_400_title = "Bad Request";
    const char* error_403_title = "Forbidden";
    const char* error_404_title = "Not Found";
    const char* error_412_title = "Precondition Failed";
    const char* error_500_title = "Internal Error";
    const char* error_503_title = "Service Unavailable";
    const char* error_400_form = "Your request has bab syntax or is inherently impossible to satisfy.\n";
    const char* error_403_form = "You do not have permission to get file from this server.\n";
    const char* error_404_form = "The request file was not fount on this server.\n";
    const char* error_412_form = "One or more preconditions specified in the request header(s) could not be satisfied";
    const char* error_500_form = "There was an unusual problem serving ths requested file.\n";
    using callback = std::function<void(void)>;
    using parse_header_func = std::function<HTTP_CODE(char*)>;
    using parse_method_func = std::function<void(HTTP_CODE)>;

protected://相关参数
    int m_sockfd;       //socket文件描述符
    int m_start_idx;    //行开始位置
    int m_check_idx;    //检查位置
    int m_read_idx;     //读取的最后位置
    int m_write_idx;
    CHECK_STATE m_check_state;  //当前检查状态
    METHOD m_method;    //请求方法
    char* m_file_ptr;   //mmap映射的文件
    char* m_url;        //请求URL
    char* m_version;    //HTTP版本
    char* m_local_fileaddr; //默认文件夹
    char* m_cookie_name;    //cookie名称
    char* m_cookie_value;   //cookie值
    int m_range_start;      //范围获取开始
    int m_range_end;        //范围获取结束
    char* m_host;           //host
    char* m_ETag;           //ETag值
    int m_content_length;   //主体长度
    time_t m_if_modified_since_time;
    time_t m_if_unmodified_since_time;
    // char m_read_buffer[READ_BUFFER_SIZE];   //读缓冲区
    char m_write_buffer[WRITE_BUFFER_SIZE]; //写缓冲区，主要写除了主体信息的其他数据
    char* m_read_buffer;
    char m_filename[FILENAME_LEN];          //文件名称
    char m_time_buffer[TIME_BUFFER_SIZE];   //时间
    struct iovec m_iov[3];  //发送数据三部分，包头+协议状态行与头部信息为一部分，主体信息为另一部分
    int m_iov_count;
    struct stat m_stat;
    std::vector<callback> m_header_func_vec;        //根据请求头部，添加响应信息头部
    std::map<std::string, parse_header_func> parse_header_func_map; //解析头部信息函数库
    std::map<METHOD, parse_method_func> parse_method_func_map;      //请求方法函数库
    std::map<float, std::string, std::greater<float>> accept_map;            //处理类型优先级
    std::map<float, std::string, std::greater<float>> accept_charset_map;    //字符集优先级
    std::map<float, std::string, std::greater<float>> accept_encoding_map;   //编码方式优先级
    std::map<float, std::string, std::greater<float>> accept_language_map;   //语言优先级

protected://协议状态
    bool m_keeplive;        //是否保持长连接，http1.1默认开启

protected://解析数据，进行响应
    LINE_STATUS parse_line(char* text);     //分析每一行
    HTTP_CODE parse_request(char* text);    //分析请求行
    HTTP_CODE parse_header(char* text);     //分析头部信息
    HTTP_CODE parse_content(char* text);    //分析主体信息
    HTTP_CODE process_read(void);           //对整个http信息进行解析
    HTTP_CODE do_request(void);             //解析无误后，文件进行处理（文件是否存在、是否有读写权限等）
    void process_write(HTTP_CODE http_code);//根据http读取状态进行回复

    int HttpWriteData(int fd, const void* dataBuf, size_t iovCount, int flag);

    void add_status_line(int status, const char* title);    //添加状态行
    void add_headers(const char* text);     //仅在出现问题时，添加相关头部信息
    void add_content(const char* text);     //仅在出现问题时，添加主体内容，正常返回文件时，采用writev返回主体、头部、请求行
    void add_blank_line(void);              //添加\r\n
    void add_date(void);                    //添加date头部信息
    void add_cookie(const char* cookieName, time_t expirse = 0, const char* path = "/", const char* domain = "192.168.138.135");//添加set_cookie字段

protected://根据特定请求头部，响应信息填充特定头部
    void if_modified_since_func(void);
    void range(void);

protected://解析头部信息
    HTTP_CODE Connection_func(char* text);
    HTTP_CODE Host_func(char* text);
    HTTP_CODE Connect_Length_func(char* text);
    HTTP_CODE Range_func(char* text);
    HTTP_CODE If_Modified_Since_func(char* text);
    HTTP_CODE If_UnModified_Since_func(char* text);
    HTTP_CODE Accept_func(char* text);
    HTTP_CODE Accept_Charset_func(char* text);
    HTTP_CODE Accept_Encoding_func(char* text);
    HTTP_CODE Accept_Language_func(char* text);
    HTTP_CODE Cookie_func(char* text);
    HTTP_CODE Authorization_func(char* text);
    HTTP_CODE If_Match_func(char* text);

protected://请求方法对应函数
    void GET_func(HTTP_CODE http_code);
    void POST_func(HTTP_CODE http_code);
    void HEAD_func(HTTP_CODE http_code);
    void OPTIONS_func(HTTP_CODE http_code);

protected://用到的相关辅助函数
template<typename... Args>              //向发送缓冲区填充数据
    void add_senddata(Args... args);
template<typename T>                   
    void write_senddata(T data);            //写入数据
template<typename T, typename... Args>
    void decompose_Args(T data, Args... args);  //分解可变参数

    time_t strtime_convert_time_t(const char* timestr); //转换时间为time_t
    void time_t_convert_strtime(time_t tmdata, char* tmbuf);
    void decompose_weight(std::map<float, std::string, std::greater<float>> strmap, char* text);//分解权重
    void init(void);        //初始化
    std::string get_cookie_value(char* text); //获取cookie;
};

template<typename T>
void HttpServer::write_senddata(T data)
{
    std::string ts = std::to_string(data);
    memcpy(&m_write_buffer[m_write_idx] , ts.c_str(), ts.size());
    m_write_idx += ts.size();
}

template<>
void HttpServer::write_senddata<const char*>(const char* data);
template<>
void HttpServer::write_senddata<char*>(char* data);


template<typename T, typename... Args>
void HttpServer::decompose_Args(T data, Args... args)        //填充http应答信息
{
    write_senddata(data);
    if constexpr (sizeof...(args) > 0)
    {
        decompose_Args(args...);
    }
}

template<typename... Args>
void HttpServer::add_senddata(Args... args)
{
    decompose_Args(args...);
}


#endif