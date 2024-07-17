#include <fstream>
#include <mutex>
#include <string>
#include <stdexcept>
#include <iostream>

#include "ngx_c_log.h"
#include "ngx_macro.h"

// static u_char err_levels[][20]  = 
// {
//     {"stderr"},    //0：控制台错误
//     {"emerg"},     //1：紧急
//     {"alert"},     //2：警戒
//     {"crit"},      //3：严重
//     {"error"},     //4：错误
//     {"warn"},      //5：警告
//     {"notice"},    //6：注意
//     {"info"},      //7：信息
//     {"debug"}      //8：调试
// };

Log* Log::m_instance = nullptr;

Log::Log(const char* FileName, LogLevel Level)
{
    this -> LogFileName = FileName;
    this -> Level = Level;
    Ofs.open(FileName, std::ios::app);
    if(!Ofs.is_open())
    {
        throw std::logic_error("open logfile fail\n");
    }
    Ofs.seekp(0, std::ios::end);
    FileCount = Ofs.tellp();
}

Log::~Log()
{
    Ofs.close();   
}

Log::Destroy_Log::~Destroy_Log()
{
    if(Log::m_instance != nullptr)
    {
        delete Log::m_instance;
    }
}

void Log::Create(const char* FileName, LogLevel Level)
{
    if(Log::m_instance == nullptr)
    {
        Log::m_instance = new Log(FileName, Level);
        static Destroy_Log DLOG;
    }
}

Log* Log::GetInstance(const char* FileName, LogLevel Level)
{
    std::once_flag create_flag;

    std::call_once(create_flag, Create, FileName, Level);

    return Log::m_instance;
}

Log* Log::GetInstance(void)
{
    return Log::m_instance;
}

void Log::ngx_setlevel(LogLevel Level)
{
    this -> Level = Level;
}

tm Log::GetTime(void)
{
    std::chrono::system_clock::time_point nowpoint = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(nowpoint);
    tm* timevalue = localtime(&t);

    return *timevalue;
}