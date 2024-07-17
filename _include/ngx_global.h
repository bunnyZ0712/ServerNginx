#ifndef NGX_GLOBAL_H
#define NGX_GLOBAL_H

#include <string>
#include <signal.h>

#include "ngx_c_socket.h"

typedef struct Item{
    Item() = default;
    Item(const char* n, const char* c) : ItemName(n), IterContent(c){}
    std::string ItemName;  //配置名称
    std::string IterContent;  //配置值

}CConfItem,*LPCConfItem;


//和设置标题有关的全局量
extern char **g_os_argv;            //原始命令行参数数组,在main中会被赋值
extern char *gp_envmem;      //指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
extern int  g_environlen;       //环境变量所占内存大小

//和进程本身有关的全局量
extern pid_t   ngx_pid;                //当前进程的pid
extern pid_t   ngx_parent;             //父进程的pid
extern int     ngx_process;            //进程类型，比如master,worker进程等
extern sig_atomic_t  ngx_reap;          //标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
                                   //一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据

#endif