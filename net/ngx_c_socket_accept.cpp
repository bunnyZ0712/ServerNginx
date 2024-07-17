#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "ngx_c_log.h"
#include "ngx_c_socket.h"
#include "ngx_c_timer.h"
#include "ngx_c_http.h"
#include "ngx_c_https.h"

void CSocket::ngx_event_accept(lpngx_connection_t oldc) //建立新连接
{
    Log* log = Log::GetInstance();

    sockaddr_in client_addr;
    socklen_t length = sizeof(sockaddr_in);
    lpngx_connection_t newc; 
    int accept4_flag = 1;
    int fd;
    int err;
    int level;

    do{
        if(accept4_flag)
        {
            fd = accept4(oldc -> fd, (sockaddr*)&client_addr, &length, SOCK_NONBLOCK);
        }
        else
        {
            fd = accept(oldc -> fd, (sockaddr*)&client_addr, &length);
        }

        //多进程多线程，在唤醒时要注意惊群现象，要进行判断，确保真正获取到资源的进程或者线程进行处理
        //防止惊群，其他进程会返回-1 错误 (11: Resource temporarily unavailable【资源暂时不可用】) 
        if(fd == -1)
        {
            err = errno;

            if(err == EAGAIN)
            {
                return;
            }
            level = NGX_LOG_ALERT;
            if(err == ECONNABORTED) //ECONNRESET错误则发生在对方意外关闭套接字后【您的主机中的软件放弃了一个已建立的连接--由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
            {
                level = NGX_LOG_ERR;
            }
            else if(err == EMFILE || err == ENFILE)
            {
                level = NGX_LOG_CRIT;
            }
            log -> ngx_log_error_core(level,errno,"CSocket::ngx_event_accept()中accept4()失败!");

            if(accept4_flag && err == ENOSYS) //accept4()函数没实现，坑爹？
            {
                accept4_flag = 0;  //标记不使用accept4()函数，改用accept()函数
                continue;         //回去重新用accept()函数搞
            }

            if (err == ECONNABORTED)  //对方关闭套接字
            {
                //这个错误因为可以忽略，所以不用干啥
                //do nothing
            }
            
            if (err == EMFILE || err == ENFILE) 
            {
                //do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
                //我这里目前先不处理吧【因为上边已经写这个日志了】；
            }            
            return;
        }
        if(m_onlineUserCount > m_worker_connections)    //超过最大连接数，踢出
        {
            if(fd != -1)
            {
                close(fd);
                log ->ngx_log_stderr(0, "m_onlineUserCount > m_worker_connections");
                return;
            }
        }
        if(!accept4_flag)
        {
            if(setnonblocking(fd) == false)
            {
                //设置非阻塞居然失败
                if(fd != -1)
                {
                    close(fd);
                }
                return; //直接返回
            }
        }
        u_char caddr[NGX_IPLENGTH_MAX];
        ngx_sock_ntop((sockaddr*)&client_addr, 1, caddr, 0);
        log -> ngx_log_error_core(NGX_LOG_ALERT, 0, caddr, " connecting");    
        // log -> ngx_log_stderr(0, caddr, "connect", fd);  
        newc = ngx_get_connection(fd);
        if(newc == nullptr)
        {
            if(fd != -1)
            {
                if(close(fd) == -1)
                {
                    log -> ngx_log_error_core(NGX_LOG_ALERT, errno,"CSocket::ngx_event_accept()中close(fd)失败!");
                }
            }
            return;
        }

        memcpy(&newc -> s_sockaddr, &client_addr, length);
        CLogicSocket* logicobj;
        std::cout << oldc -> listening ->port << std::endl;
        switch(oldc -> listening ->port)
        {
            case 80 : logicobj = new HttpServer;
                      break;
            case 443: logicobj = new HttpsServer(fd);
                      while(!((HttpsServer*)logicobj) ->GetSSL() ->Accept()){}
                      break;
        }
        m_onlineUserCount++;
        newc -> logicObj = logicobj;
        newc -> ReadFunc = bind(&CLogicSocket::ReadData, logicobj,  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
        newc -> WriteFunc = bind(&CLogicSocket::WriteData, logicobj,  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
        newc -> listening = oldc -> listening;
        newc ->whandler = &CSocket::ngx_write_request_handler;//所有都绑定发送函数，只有需要epoll触发时，直接修改为写事件就行
        newc ->curStat = _PKG_HD_INIT;              //初始化收包状态
        newc ->irecvlen = sizeof(COMM_PKG_HEADER);  //开始先收包头
        newc ->precvbuf = newc ->dataHeadInfo;
        newc ->precvMemPointer  = nullptr;   
        newc ->FloodAttackCount = 0; 
        newc ->phandler = &CSocket::ngx_evhup_handler;
        if(ngx_epoll_opr_event(newc, READEVENT, 0, EPOLL_CTL_ADD) == false) //读事件放在该函数中添加，因为要判断是ET还是LT
        {
            //增加事件失败，失败日志在ngx_epoll_add_event中写过了，因此这里不多写啥；
            ngx_close_connection(newc);
            return; //直接返回                
        }
        ngx_add_check(newc); //加入心跳包检测
        break;
    }while(1);
}