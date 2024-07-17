#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <sys/wait.h>

#include "ngx_c_log.h"
#include "ngx_macro.h"
#include "ngx_global.h"

static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext);  //信号处理函数，目前就一个
static void ngx_process_get_status(void);   //获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程

typedef struct{
    int signo;                          //信号编号
    const char* signame;                //信号名字
    void  (*handler)(int signo, siginfo_t *siginfo, void *ucontext);    //信号处理函数
}ngx_signal_t;

ngx_signal_t signals[] = {
    // signo      signame             handler
    { SIGHUP,    "SIGHUP",           ngx_signal_handler },        //终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    { SIGINT,    "SIGINT",           ngx_signal_handler },        //标识2   
	{ SIGTERM,   "SIGTERM",          ngx_signal_handler },        //标识15
    { SIGCHLD,   "SIGCHLD",          ngx_signal_handler },        //子进程退出时，父进程会收到这个信号--标识17
    { SIGQUIT,   "SIGQUIT",          ngx_signal_handler },        //标识3
    { SIGIO,     "SIGIO",            ngx_signal_handler },        //指示一个异步I/O事件【通用异步I/O信号】
    { SIGSYS,    "SIGSYS, SIG_IGN",  nullptr               },        //我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
                                                                  //所以我们把handler设置为NULL，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉我）
    //...日后根据需要再继续增加
    { 0,         NULL,               nullptr               }         //信号对应的数字至少是1，所以可以用0作为一个特殊标记
};

bool ngx_init_signals(void)
{
    struct sigaction sa;
    ngx_signal_t *sig;
    Log* log = Log::GetInstance();
    for(sig = signals; sig -> signo; sig++) //注册信号处理函数
    {
        if(sig -> handler != nullptr)
        {
            sa.sa_sigaction = sig -> handler;
            sa.sa_flags = SA_SIGINFO;           //可以传入更多信息
        }
        else
        {
            sa.sa_handler = SIG_IGN;
        }

        sigemptyset(&sa.sa_mask);
        if(sigaction(sig -> signo, &sa, nullptr) == -1)
        {
            log->ngx_log_error_core(NGX_LOG_EMERG, errno, "sigaction", sig -> signame, "failed");
            return false;
        }
        else
        {
            log->ngx_log_stderr(0, "sigaction", sig -> signame, "succed");
        }
    }
    return true;
}

static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext)
{
    Log* log = Log::GetInstance();
    int err = errno;
    ngx_signal_t* sig;
    int status;

    for(sig = signals; sig -> signo != 0; sig++)    //寻找对应信号
    {
        if(sig -> signo == signo)
            break;
    }

   if(ngx_process == NGX_PROCESS_MASTER)    //master进程信号处理
   {
        switch (sig -> signo)
        {
        case SIGCHLD :
            ngx_reap = 1;  ////标记子进程状态变化，日后master主进程的for(;;)循环中可能会用到这个变量【比如重新产生一个子进程】
            break;
        default:        //后续补充其他信号的
            break;
        }
   }
   else if(ngx_process == NGX_PROCESS_WORKER)   //worker进程信号处理
   {
        
   }
   else
   {
        //其他进程保留
   }

   if(signo && siginfo->si_pid) //记录发送的信号类型以及发送信号的进程
   {
        log -> ngx_log_error_core(NGX_LOG_NOTICE, 0, "signal ", signo, " ", strsignal(signo), " received from ", siginfo->si_pid);
   }
   else
   {
        log -> ngx_log_error_core(NGX_LOG_NOTICE, 0, "signal ", signo, strsignal(signo), " received");
   }

   if(signo == SIGCHLD)
   {
        ngx_process_get_status();
   }
    errno = err;
}

//获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程
static void ngx_process_get_status(void)
{
    int status;
    pid_t pid;
    int one=0; //抄自官方nginx，应该是标记信号正常处理过一次
    Log* log = Log::GetInstance();

    while(true)
    {
        pid = waitpid(-1, &status, WNOHANG);

        if(pid == 0)
        {
            return;
        }
        else if(pid == -1)
        {
            if(errno == EINTR)
                continue;
            else if(errno == ECHILD && one) //回收子进程已经被处理过一次了
            {
                return;
            }
            if(errno == ECHILD)
            {
                log -> ngx_log_error_core(NGX_LOG_INFO,errno,"waitpid failed!");
                return;
            }
            log -> ngx_log_error_core(NGX_LOG_INFO,errno,"waitpid failed!");
            return;
        }
        else if(pid > 0)
            break;
    }
    one = 1;
    if(WIFSIGNALED(status))
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, 0, "pid = ", pid, " exited on signal ", WTERMSIG(status), " ",strsignal(WTERMSIG(status)));
    }
    else if(WIFEXITED(status))
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, 0, "pid = ", pid, " exited with code ", " ",WEXITSTATUS(status));
    }
}

