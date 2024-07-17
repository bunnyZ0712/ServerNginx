#ifndef NGX_C_LOG_H
#define NGX_C_LOG_H

#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <sys/statfs.h>

enum LogLevel{
    NGX_LOG_STDERR,         //控制台错误【stderr】：最高级别日志，日志的内容不再写入log参数指定的文件，而是会直接将日志输出到标准错误设备比如控制台屏幕
    NGX_LOG_EMERG,          //紧急 【emerg】
    NGX_LOG_ALERT,          //警戒 【alert】
    NGX_LOG_CRIT,           //严重 【crit】
    NGX_LOG_ERR,            //错误 【error】：属于常用级别
    NGX_LOG_WARN,           //警告 【warn】：属于常用级别
    NGX_LOG_NOTICE,         //注意 【notice】
    NGX_LOG_INFO,           //信息 【info】
    NGX_LOG_DEBUG,          //调试 【debug】：最低级别
    NGX_LOG_NUM
};

class Log{
private:
    Log(const char* FileName, LogLevel Level);

public:
    ~Log();

private:
    static Log* m_instance;
    class Destroy_Log{
    public:
        ~Destroy_Log();
    };
    static void Create(const char* FileName, LogLevel Level);
    tm GetTime(void);
public:
    static Log* GetInstance(const char* FileName, LogLevel Level);
    static Log* GetInstance(void);
    void ngx_setlevel(LogLevel Level);
    template<typename... Args>
    void ngx_log_stderr(int err, Args... args); //写入标准输入
    template<typename... Args>
    void ngx_log_error_core(int level, int err, Args... args);  //写入日志
private:
    const char* LogFileName;    //日志文件名称
    LogLevel Level;     //当前等级
    std::ofstream Ofs;
    size_t FileCount;
    u_char err_levels[NGX_LOG_NUM][20]  = 
    {
        {"stderr"},    //0：控制台错误
        {"emerg"},     //1：紧急
        {"alert"},     //2：警戒
        {"crit"},      //3：严重
        {"error"},     //4：错误
        {"warn"},      //5：警告
        {"notice"},    //6：注意
        {"info"},      //7：信息
        {"debug"}      //8：调试
    };
};

//err：对应系统函数出错时errno 只要不是0就进行转换
template<typename... Args>
void Log::ngx_log_stderr(int err, Args... args)
{
    tm timevalue = GetTime();
    char timebuf[32];
    bzero(timebuf, 32);
    strftime(timebuf, 32, "%Y-%m-%d %H:%M:%S", &timevalue);
    //写入标准输入
    if(err)
    {
        ((std::cerr << timebuf << " :" << err_levels[0] << " ") << ... << args) << " ( " << err << " :" << strerror(err) << " )" << std::endl;
    }
    else
    {
        ((std::cerr << timebuf << " :") << ... << args) << std::endl;
    }
}

//level:当前错误的等级
//err：对应系统函数出错时errno 只要不是0就进行转换
template<typename... Args>
void Log::ngx_log_error_core(int level, int err, Args... args)
{
    tm timevalue = GetTime();
    char timebuf[32];
    bzero(timebuf, 32);
    strftime(timebuf, 32, "%Y-%m-%d %H:%M:%S", &timevalue); //获取当前时间
    if(level >= this -> Level)
    {
        std::cerr << "Level lower" << std::endl;
    }
    //写入文件
    if(err)
    {
        ((Ofs << timebuf << " :"<< err_levels[level] << " ") << ... << args) << " ( " << err << " :" << strerror(err) << " )" << std::endl;
    }
    else
    {
        ((Ofs << timebuf << " :") << ... << args) << std::endl;
    }
    
    if(Ofs.fail())
    {
        struct statfs sta;
        statfs("/", &sta);  //写入文件失败，看是不是磁盘满了
        if(sta.f_bavail == 0)
        {

        }
        else
        {
            std::cerr << "write log fail" << std::endl;
        }
    }
}

#endif