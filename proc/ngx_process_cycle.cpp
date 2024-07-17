#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "ngx_c_log.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_c_timer.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_slogic.h"

static void ngx_start_worker_processes(int threadnums); //根据参数创建对应数量子进程
static int ngx_spawn_process(int inum,const char *pprocname); //产生一个子进程
static void ngx_worker_process_cycle(int inum,const char *pprocname);  //worker子进程的功能函数
static void ngx_worker_process_init(int inum);  //子进程初始化

void ngx_master_process_cycle(void)
{
    sigset_t set;
    Log* log = Log::GetInstance();
    CConfig* cc = CConfig::GetInstance();

    sigemptyset(&set);
    //下列这些信号在执行本函数期间不希望收到【考虑到官方nginx中有这些信号，老师就都搬过来了】（保护不希望由信号中断的代码临界区）
    //建议fork()子进程时学习这种写法，防止信号的干扰；
    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符
    //.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......

    if(sigprocmask(SIG_BLOCK, &set, nullptr) == -1)
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_master_process_cycle()中sigprocmask()失败!");
    }

    ngx_setproctitle("master process test");
    int  workernum;
    if(cc -> GetNum<int>("WorkerProcesses", workernum) == false)
    {
        log -> ngx_log_stderr(NGX_LOG_STDERR, "WorkerProcesses Get Failed");
    }
    ngx_start_worker_processes(workernum);

    sigemptyset(&set);
    while(true)
    {
        sigsuspend(&set);

        sleep(1);
    }
}

//根据参数创建对应数量子进程
static void ngx_start_worker_processes(int threadnums)
{
    for(int i = 0; i < threadnums; i++)
    {
        ngx_spawn_process(i, "worker process test");
    }
}

//产生一个子进程
//inum:进程编号
//pprocname:进程名称
int ngx_spawn_process(int inum,const char *pprocname)
{
    pid_t pid;
    Log* log = Log::GetInstance();

    pid = fork();
    switch(pid)
    {
        case -1 :
            log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "frok failed");
            break;
        case 0  :
            ngx_parent = ngx_pid;
            ngx_pid = getpid();
            ngx_worker_process_cycle(inum, pprocname);
            break;
        default :
            break;
    }

    return pid;
}

//worker子进程的功能函数
//inum:进程编号
//pprocname:进程名称
static void ngx_worker_process_cycle(int inum,const char *pprocname)
{
    Log* log = Log::GetInstance();
    Timer* timer = Timer::GetInstance();
    CSocket* cs =  CSocket::GetInstance();
    int ret;

    ngx_process = NGX_PROCESS_WORKER;
    ngx_worker_process_init(inum);
    ngx_setproctitle(pprocname);
    log -> ngx_log_error_core(NGX_LOG_NOTICE, 0, pprocname, ngx_pid, "启动并开始运行......!");

    while(true) //逻辑部分
    {
        ret = cs ->ngx_epoll_process_events(timer->SetTime());

        if(ret == 0)
        {
            timer ->CheckTimer();
        }
    }
}

//子进程初始化
//inum:进程编号
static void ngx_worker_process_init(int inum)
{
    sigset_t set;
    Log* log = Log::GetInstance();
    CSocket* cs =  CSocket::GetInstance();
    sigemptyset(&set);

    if(sigprocmask(SIG_SETMASK, &set, nullptr) == -1)
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_worker_process_init()中sigprocmask()失败!");
    }
    CThread::GetInstance(5, 5);
    cs ->ngx_epoll_init();
    // CLogicSocket();

    //后续扩充
}