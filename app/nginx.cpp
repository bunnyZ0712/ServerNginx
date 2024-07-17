#include <iostream>
#include <unistd.h>
#include <signal.h>

#include "ngx_func.h"
#include "ngx_c_conf.h"
#include "ngx_c_log.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_c_socket.h"
#include "ngx_c_timer.h"

using namespace std;

//本文件用的函数声明
static void freeresource();
static bool ngx_sys_init(void); //初始化函数

//和设置标题有关的全局量
int     g_os_argc;              //参数个数 
char    **g_os_argv;            //原始命令行参数数组,在main中会被赋值
char    *gp_envmem=NULL;        //指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
int     g_daemonized=0;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了
int     g_environlen = 0;       //环境变量所占内存大小


//和进程本身有关的全局量
pid_t   ngx_pid;                //当前进程的pid
pid_t   ngx_parent;             //父进程的pid
int     ngx_process;            //进程类型，比如master,worker进程等

sig_atomic_t  ngx_reap;         //标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
                                   //一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】

int main(int argc, const char* argv[])
{
    int exitcode = 0;
    g_os_argc = argc;       //初始化传入参数
    g_os_argv = const_cast<char**>(argv);
    if(ngx_sys_init() == false)
    {
        std::cerr << "ngx_sys_init failed" << std::endl;
        return -1;
    }

    CConfig* cc = CConfig::GetInstance();
    Log* log = Log::GetInstance();
    int daemoncode; //守护进程开启标志
    daemoncode = cc -> GetNum<int>("Daemon", daemoncode);   //获取守护进程开启标志
    
    if(daemoncode == 1) //开启守护进程
    {
        int ret = ngx_daemon();
        if(ret == -1)  //守护进程fork失败
        {
            log -> ngx_log_stderr(0, "daemon fork failed");
            freeresource();
            return -1;
        }
        else if(ret == 1)//父进程返回
        {
            log -> ngx_log_stderr(0, "NO1 process exit");
            freeresource();
            return 0;
        }
        g_daemonized = 1;
    }
    
    if(ngx_init_signals() == false) //初始化信号处理函数
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_init_signals failed");
    }

    if(CSocket::GetInstance() ->Initialize() == false)  //初始化socket，打开相关端口
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, 0, "g_socket.Initialize() failed");
    }

    ngx_master_process_cycle(); //开启worker线程

    log -> ngx_log_stderr(0, "master process exit");
    freeresource();     //释放资源
    
    return 0;
}

static bool ngx_sys_init(void)
{
    ngx_pid = getpid();
    ngx_parent = getppid();
    ngx_reap = 0;
    ngx_process = NGX_PROCESS_MASTER;
    CConfig* cc = CConfig::GetInstance();   //初始化配置文件
    if(cc -> Load("./nginx.conf") == false)
    {
        std::cerr << "configure file load failed" << endl;
        return false;
    }

    const char* logaddr = cc -> GetString("LogAddr");   //初始化日志文件
    if(logaddr == nullptr)
    {
        Log* log = Log::GetInstance(NGX_ERROR_LOG_PATH, NGX_LOG_DEBUG); //如果没有配置日志文件，则采用默认路径
    }
    else
    {
        Log* log = Log::GetInstance(logaddr, NGX_LOG_DEBUG);
        //Log* log = Log::GetInstance("./test.log", NGX_LOG_DEBUG);
    }
    if(Timer::GetInstance() == nullptr)
    {
        std::cerr << "Timer init failed" << endl;
        return false;        
    }

    CSocket::GetInstance();
    ngx_init_setproctitle();    //初始化环境变量与传入参数

    return true;
}

static void freeresource()
{
    if(gp_envmem != nullptr)
    {
        delete[] gp_envmem;
        gp_envmem = nullptr;
    }
}