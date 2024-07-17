#ifndef NGX_FUNC_H
#define NGX_FUNC_H

//设置可执行程序标题相关函数
void  ngx_init_setproctitle(void); //将环境变量相关内容存到其他地方
void  ngx_setproctitle(const char *title); //设置进程名称

bool ngx_init_signals(void); //初始化信号
void ngx_master_process_cycle(void);    //创建worker子进程
int ngx_daemon(void);   //创建守护进程

#endif