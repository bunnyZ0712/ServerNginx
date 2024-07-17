#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "ngx_c_log.h"
#include "ngx_global.h"

int ngx_daemon(void)
{
    pid_t pid;
    Log* log = Log::GetInstance();

    pid = fork();
    switch (pid)
    {
        case -1 :
            log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_daemon fork failed");
            return -1;
        case 0 :
            break;
        default:
            return 1;
    }

    ngx_parent = ngx_pid;
    ngx_pid = getpid();

    if(setsid() == -1)
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_daemon setsid failed");
        return false;
    }

    umask(0);

    int fd = open("/dev/null", O_RDWR);
    switch (pid)
    {
    case -1 :
        log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_daemon open /dev/null failed");
        return -1;
    default:
        break;
    }   
    if(dup2(fd, STDIN_FILENO) == -1)
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_daemon dup STDIN_FILENO failed");
        return -1;        
    }
    if(dup2(fd, STDOUT_FILENO) == -1)
    {
        log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_daemon dup STDOUT_FILENO failed");
        return -1;        
    }
    if (fd > STDERR_FILENO)  //fd应该是3，这个应该成立
     {
        if (close(fd) == -1)  //释放资源这样这个文件描述符就可以被复用；不然这个数字【文件描述符】会被一直占着；
        {
            log -> ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon close failed");
            return -1;
        }
    }
    return 0; //子进程返回0
}