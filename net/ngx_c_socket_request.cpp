#include "ngx_c_socket.h"
#include "ngx_c_log.h"
#include "ngx_c_memory.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_timer.h"
#include "ngx_c_slogic.h"

#include <unistd.h>

//接收数据专用函数--引入这个函数是为了方便，如果断线，错误之类的，这里直接 释放连接池中连接，然后直接关闭socket，以免在其他函数中还要重复的干这些事
//参数c：连接池中相关连接
//参数buff：接收数据的缓冲区
//参数buflen：要接收的数据大小
//返回值：返回-1，则是有问题发生并且在这里把问题处理完毕了，调用本函数的调用者一般是可以直接return
//        返回>0，则是表示实际收到的字节数
ssize_t CSocket::recvproc(lpngx_connection_t c, char *buff, ssize_t buflen)
{
    Log* log = Log::GetInstance();
    ssize_t n;
    // n = recv(c ->fd, buff, buflen, 0);
    n = c ->ReadFunc(c ->fd, buff, buflen, 0);
    // log -> ngx_log_stderr(0, n, "aaa");
    // log -> ngx_log_stderr(0, "ret:", n);
    if(n == 0)
    {
        Timer* timer = Timer::GetInstance();
        u_char caddr[NGX_IPLENGTH_MAX];
        ngx_sock_ntop(&c ->s_sockaddr, 1, caddr, 0);
        log -> ngx_log_stderr(0, caddr, " close"); 
        ngx_epoll_del_event(c); //删除监听事件
        m_onlineUserCount--;
        timer->AddTimer(10000, 1, std::bind(CSocket::ngx_time_tofree_connection, std::placeholders::_1), c);
        std::unique_lock<std::mutex> PingLock(m_PingMutex);
        m_timerQueuemap.erase(m_timerQueuemap.find(c ->storePingTime));
        return 0;
    }
    else if(n == -1)
    {
        if(errno == EAGAIN || errno ==EWOULDBLOCK)  //没有数据可读，LT模式按道理不会出现
        {
            log ->ngx_log_stderr(errno,"CSocket::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！", std::this_thread::get_id());//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回            
        }
        else if(errno == EINTR)
        {
            log ->ngx_log_stderr(errno,"CSocket::recvproc()中errno == EINTR成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回           
        }

        //所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；
        if(errno == ECONNRESET)  //#define ECONNRESET 104 /* Connection reset by peer */
        {
            //如果客户端没有正常关闭socket连接，却关闭了整个运行程序【真是够粗暴无理的，应该是直接给服务器发送rst包而不是4次挥手包完成连接断开】，那么会产生这个错误            
            //10054(WSAECONNRESET)--远程程序正在连接的时候关闭会产生这个错误--远程主机强迫关闭了一个现有的连接
            //算常规错误吧【普通信息型】，日志都不用打印，没啥意思，太普通的错误
            //do nothing
            Timer* timer = Timer::GetInstance();
            u_char caddr[NGX_IPLENGTH_MAX];
            ngx_sock_ntop(&c ->s_sockaddr, 1, caddr, 0);
            log -> ngx_log_stderr(0, caddr, " close"); 
            ngx_epoll_del_event(c); //删除监听事件
            timer->AddTimer(10000, 1, std::bind(CSocket::ngx_time_tofree_connection, std::placeholders::_1), c);
            std::unique_lock<std::mutex> PingLock(m_PingMutex);
            m_timerQueuemap.erase(m_timerQueuemap.find(c ->lastPingTime));
            return 0;
            //....一些大家遇到的很普通的错误信息，也可以往这里增加各种，代码要慢慢完善，一步到位，不可能，很多服务器程序经过很多年的完善才比较圆满；
        }
        else if(errno == ENOTCONN)
        {
            log ->ngx_log_stderr(errno, "CSocket::recvproc()中errno == ENOTCONN");
        }
        else
        {
            //能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            log ->ngx_log_stderr(errno,"CSocket::recvproc()中发生错误，我打印出来看看是啥错误！", std::this_thread::get_id());  //正式运营时可以考虑这些日志打印去掉
        } 
        ngx_epoll_del_event(c); //能走到这里，不是一般的错误，需要关闭连接回收资源
        ngx_close_connection(c);

        return -1;
    }

    return n;
}

void CSocket::ngx_wait_request_handler_proc_p1(lpngx_connection_t c, bool& floodflag)
{
    CMemory* mm = CMemory::GetInstance();
    Log* log = Log::GetInstance();
    void* t = c ->dataHeadInfo;
    LPCOMM_PKG_HEADER BHData = static_cast<LPCOMM_PKG_HEADER>(t);
    unsigned short e_pkgLen;         //注意这里网络序转本机序，所有传输到网络上的2字节数据，都要用htons()转成网络序，所有从网络上收到的2字节数据，都要用ntohs()转成本机序
    //e_pkgLen = ntohs(BHData ->pkgLen); //ntohs/htons的目的就是保证不同操作系统数据之间收发的正确性，【不管客户端/服务器是什么操作系统，发送的数字是多少，收到的就是多少】
                                    //不明白的同学，直接百度搜索"网络字节序" "主机字节序" "c++ 大端" "c++ 小端" 不需要转化！
    //e_pkgLen 为包头+包体总长度                                
    e_pkgLen = BHData ->pkgLen;
    // log ->ngx_log_stderr(0, "Len:", e_pkgLen);
    if(e_pkgLen > (_PKG_MAX_LENGTH - 1000))  //恶意包判断,长度大于最大长度
    {
        c ->curStat = _PKG_HD_INIT;
        c ->irecvlen =  m_iLenPkgHeader;  
        c ->precvbuf = c ->dataHeadInfo;
    }
    else if(e_pkgLen < m_iLenPkgHeader) //长度小于包头长，可能包出现了错误
    {
        c ->curStat = _PKG_HD_INIT;
        c ->irecvlen =  m_iLenPkgHeader;  
        c ->precvbuf = c ->dataHeadInfo;        
    }
    else
    {
        void *pTmpBuffer = mm->AllocMemory(m_iLenMsgHeader + e_pkgLen, false);   //消息头+包头+包体
        c ->precvbuf = static_cast<char*>(pTmpBuffer);
        c ->precvMemPointer = static_cast<char*>(pTmpBuffer);;

        LPSTRUC_MSG_HEADER ptmpMsgHeader = static_cast<LPSTRUC_MSG_HEADER>(pTmpBuffer);
        ptmpMsgHeader ->pConn = c;
        ptmpMsgHeader ->iCurrsequence = c ->iCurrsequence;
        pTmpBuffer = static_cast<char*>(pTmpBuffer) + m_iLenMsgHeader;                 //往后跳，跳过消息头，指向包头
        memcpy(pTmpBuffer, BHData, m_iLenPkgHeader); //直接把收到的包头拷贝进来
        // log ->ngx_log_stderr(0, "crc:", BHData ->crc32);

        if(e_pkgLen == m_iLenPkgHeader) //只有包头
        {
            floodflag = TestFlood(c);
            if(floodflag == true)
            {
                return;
            }
            ngx_wait_request_handler_proc_plast(c);
        }
        else
        {
            //开始收包体，注意写法
            c->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
            c->precvbuf = static_cast<char*>(pTmpBuffer) + m_iLenPkgHeader;  //pTmpBuffer指向包头，这里 + m_iLenPkgHeader后指向包体位置
            c->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体            
        }
    }
}

void CSocket::ngx_wait_request_handler_proc_plast(lpngx_connection_t c)
{
    Log* log = Log::GetInstance();
    CThread* ct = CThread::GetInstance();
    int irmqc = 0;
    //inMsgRecvQueue(c ->pnewMemPointer, irmqc);
    ct ->AddTask(std::bind(&CLogicSocket::threadRecvProcFunc, c ->logicObj, c ->precvMemPointer));
    //业务逻辑
     //内存不再需要释放，因为你收完整了包，这个包被上边调用inMsgRecvQueue()移入消息队列，那么释放内存就属于业务逻辑去干，不需要回收连接到连接池中干了
    c->precvMemPointer  = nullptr;
    c ->curStat = _PKG_HD_INIT;     //数据处理完成后，又开始新一轮接收包头
    c ->precvbuf = c ->dataHeadInfo;
    c ->irecvlen = m_iLenPkgHeader; 
    //这里不需要释放连接资源，因为只是本次通信结束，还可能有下次，连接并未断开，等客户端断开之后再释放回资源池
}


void CSocket::ngx_read_request_handler_ET(lpngx_connection_t c)
{
    CThread* ct = CThread::GetInstance();
    Log* log = Log::GetInstance();

    ct ->AddTask(std::bind(CSocket::recvproc_ET, c));
}

//ET模式下，通过多线程读取数据并处理业务，所以在读取部分就需要加锁，否则会导致线程冲突
void CSocket::recvproc_ET(void* arg)
{
    lpngx_connection_t c = static_cast<lpngx_connection_t>(arg);
    Log* log = Log::GetInstance();
    CSocket* cs = CSocket::GetInstance();
    ssize_t ret;
    int flag = 0;   //标志收完数据，已完成业务处理函数，因为ET需要通过循环保证数据接收完成
    bool floodflag = false;

    c ->logicPorcMutex.lock();
    while(flag == 0 && floodflag == false)
    {   
        ret = cs ->recvproc(c, c ->precvbuf, c ->irecvlen);
        if(ret < 0)
        {
            c ->logicPorcMutex.unlock();
            return;
        }
        else if(ret == 0)
        {
            c ->logicPorcMutex.unlock();
            return;
        }
        if(c ->curStat == _PKG_HD_INIT) //初始状态，准备接收数据包头
        {
            if(ret == cs ->m_iLenPkgHeader)  //判断读取数据与包头长度，小于则继续读取包头
            {
                cs ->ngx_wait_request_handler_proc_p1_ET(c, flag, floodflag);
            }
            else    
            {
                c ->curStat = _PKG_HD_RECVING;
                c ->precvbuf += ret;
                c ->irecvlen -= ret;
            }
        }
        else if(c ->curStat == _PKG_HD_RECVING) //接收包头中，包头不完整，继续接收中
        {
            if(c ->irecvlen == ret) //判断读取数据与剩余包头长度，小于则继续读取包头
            {
                cs ->ngx_wait_request_handler_proc_p1_ET(c, flag, floodflag);
            }
            else
            {
                c ->precvbuf += ret;
                c ->irecvlen -= ret;
            }
        }
        else if(c ->curStat == _PKG_BD_INIT)    //包头刚好收完，准备接收包体
        {
            floodflag = cs ->TestFlood(c);
            if(floodflag == true)
            {
                break;
            }
            if(c ->irecvlen == ret)
            {
                cs ->ngx_wait_request_handler_proc_plast_ET(c); //处理完业务
                break;
            }
            else
            {
                c ->curStat = _PKG_BD_RECVING;
                c ->precvbuf += ret;
                c ->irecvlen -= ret;            
            }
        }
        else if(c ->curStat == _PKG_BD_RECVING) //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态
        {
            //接收包体中，包体不完整，继续接收中
            if(c->irecvlen == ret)
            {
                //包体收完整了
                cs ->ngx_wait_request_handler_proc_plast_ET(c); //处理完业务
                break;
            }
            else
            {
                //包体没收完整，继续收
                c->precvbuf += ret;
                c->irecvlen = ret;
            }        
        }
    }
    if(floodflag == true)   //flood标志，如果满足，则主动断开连接
    {
        cs ->ngx_free_con_self(c);
        c ->logicPorcMutex.unlock();
    }
}

//LT模式下，读取数据在主线程中进行，不存在线程冲突问题，所以不需要加锁，但是在业务处理以及写数据部分，通过多线程处理，所以需要加锁
void CSocket::ngx_read_request_handler(lpngx_connection_t c)    //设置数据来时的读处理函数
{
    Log* log = Log::GetInstance();
    bool floodflag = false;

    ssize_t ret;
    ret = recvproc(c, c ->precvbuf, c ->irecvlen);  //LT如果没有读取完缓冲区的数据，会一直触发，所以不需要加循环
    if(ret < 0)
    {
        return;
    }
    else if(ret == 0)   //客户端断开连接
    {
        return;
    }
    // log ->ngx_log_stderr(0, "LT", ret, "  ", (int)c ->curStat);
    if(c ->curStat == _PKG_HD_INIT) //初始状态，准备接收数据包头
    {
        if(ret == m_iLenPkgHeader)  //判断读取数据与包头长度，小于则继续读取包头
        {
            ngx_wait_request_handler_proc_p1(c, floodflag);
            if(floodflag == true)   //flood标志，如果满足，则主动断开连接
            {
                ngx_free_con_self(c);
                return;
            }
        }
        else    
        {
            c ->curStat = _PKG_HD_RECVING;
            c ->precvbuf += ret;
            c ->irecvlen -= ret;
        }
    }
    else if(c ->curStat == _PKG_HD_RECVING) //接收包头中，包头不完整，继续接收中
    {
        if(c ->irecvlen == ret) //判断读取数据与剩余包头长度，小于则继续读取包头
        {
            ngx_wait_request_handler_proc_p1(c, floodflag);
            if(floodflag == true)   //flood标志，如果满足，则主动断开连接
            {
                ngx_free_con_self(c);
                return;
            }
        }
        else
        {
            c ->precvbuf += ret;
            c ->irecvlen -= ret;
        }
    }
    else if(c ->curStat == _PKG_BD_INIT)    //包头刚好收完，准备接收包体
    {
        floodflag = TestFlood(c);
        if(floodflag == true)
        {
            ngx_free_con_self(c);
            return;
        }
        if(c ->irecvlen == ret)
        {         
            ngx_wait_request_handler_proc_plast(c);
        }
        else
        {
            c ->curStat = _PKG_BD_RECVING;
            c ->precvbuf += ret;
            c ->irecvlen -= ret;            
        }
    }
    else if(c ->curStat == _PKG_BD_RECVING) //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态
    {
        //接收包体中，包体不完整，继续接收中
        if(c->irecvlen == ret)
        {
            //包体收完整了
            ngx_wait_request_handler_proc_plast(c);
        }
        else
        {
            //包体没收完整，继续收
            c->precvbuf += ret;
			c->irecvlen = ret;
        }        
    }
}

void CSocket::sendMsg(void* arg)
{
    Log* log = Log::GetInstance();
    CSocket* cs = CSocket::GetInstance();
    CMemory* mm = CMemory::GetInstance();
    LPSTRUC_MSG_HEADER p_MsgHeader = static_cast<LPSTRUC_MSG_HEADER>(arg);
    lpngx_connection_t c = p_MsgHeader ->pConn;
    int ret;

    if(p_MsgHeader ->iCurrsequence != c ->iCurrsequence)    //连接已断开
    {
        if(c ->deleteFlag)  //申请过才需要释放，http协议利用iovec发送数据的，不需要释放
        {
            mm ->FreeMemory(c ->psendMemPointer);       //记得释放发送buf
        }
        c ->psendMemPointer = nullptr;
        c ->psendbuf = nullptr;
        c ->isendlen = 0;
        c ->logicPorcMutex.unlock();
        log ->ngx_log_stderr(0, "connect has closed");
        return;
    }

    while(true)
    {

        // ret = send(c ->fd, c ->psendbuf, c ->isendlen, MSG_NOSIGNAL);
        ret = c ->WriteFunc(c ->fd, c ->psendbuf, c ->isendlen, MSG_NOSIGNAL);
        log ->ngx_log_stderr(0, "send22", ret, '\t',c ->isendlen);
        if(ret < c ->isendlen && ret >= 0)
        {
            c ->psendbuf += ret;
            c ->isendlen -= ret;
            cs ->ngx_epoll_opr_event(c, WRITEEVENT, 0, EPOLL_CTL_MOD);    //采用LT，在触发写事件时，总是会重复触发
            break;
        }
        else if(ret == -1)
        {
            if(errno == EAGAIN) //发送缓冲区满
            {
                cs ->ngx_epoll_opr_event(c, WRITEEVENT, 0, EPOLL_CTL_MOD);
                break;
            }
            else if(errno == EINTR)
            {
                continue;
            }
            else if(errno == EPIPE || errno == ECONNRESET)  //EPIPE：一次都没有发送对端就断开；ECONNRESET：发送成功过，但后续发送时对端断开了
            {
                if(c ->deleteFlag)
                {
                    mm ->FreeMemory(c ->psendMemPointer);   //记得释放发送buf
                }
                c ->psendMemPointer = nullptr;
                c ->psendbuf = nullptr;
                c ->isendlen = 0;
                break;
            }
        }
        else if(ret == c ->isendlen)    //数据发送完成
        {
            if(c ->deleteFlag)
            {
                mm ->FreeMemory(c ->psendMemPointer);   //记得释放发送buf
            }
            c ->psendMemPointer = nullptr;
            c ->psendbuf = nullptr;
            c ->isendlen = 0;
            c ->logicPorcMutex.unlock();
            log ->ngx_log_stderr(0, "send");
            break;
        }
    }
}

void CSocket::ngx_write_request_handler(lpngx_connection_t c)
{
    CThread* ct = CThread::GetInstance();
    Log* log = Log::GetInstance();

    ct ->AddTask(std::bind(sendRemainMsg, c));
}

void CSocket::ngx_wait_request_handler_proc_p1_ET(lpngx_connection_t c, int& readflag, bool& floodflag)
{
    CMemory* mm = CMemory::GetInstance();
    Log* log = Log::GetInstance();
    void* t = c ->dataHeadInfo;
    LPCOMM_PKG_HEADER BHData = static_cast<LPCOMM_PKG_HEADER>(t);
    unsigned short e_pkgLen;         //注意这里网络序转本机序，所有传输到网络上的2字节数据，都要用htons()转成网络序，所有从网络上收到的2字节数据，都要用ntohs()转成本机序
    //e_pkgLen = ntohs(BHData ->pkgLen); //ntohs/htons的目的就是保证不同操作系统数据之间收发的正确性，【不管客户端/服务器是什么操作系统，发送的数字是多少，收到的就是多少】
                                    //不明白的同学，直接百度搜索"网络字节序" "主机字节序" "c++ 大端" "c++ 小端" 不需要转化！
    //e_pkgLen 为包头+包体总长度                                
    e_pkgLen = BHData ->pkgLen;
    // log ->ngx_log_stderr(0, "Len:", e_pkgLen);
    if(e_pkgLen > (_PKG_MAX_LENGTH - 1000))  //恶意包判断,长度大于最大长度
    {
        c ->curStat = _PKG_HD_INIT;
        c ->irecvlen =  m_iLenPkgHeader;  
        c ->precvbuf = c ->dataHeadInfo;
    }
    else if(e_pkgLen < m_iLenPkgHeader) //长度小于包头长，可能包出现了错误
    {
        c ->curStat = _PKG_HD_INIT;
        c ->irecvlen =  m_iLenPkgHeader;  
        c ->precvbuf = c ->dataHeadInfo;        
    }
    else
    {
        void *pTmpBuffer = mm->AllocMemory(m_iLenMsgHeader + e_pkgLen, false);   //消息头+包头+包体
        c ->precvbuf = static_cast<char*>(pTmpBuffer);
        c ->precvMemPointer = static_cast<char*>(pTmpBuffer);;

        LPSTRUC_MSG_HEADER ptmpMsgHeader = static_cast<LPSTRUC_MSG_HEADER>(pTmpBuffer);
        ptmpMsgHeader ->pConn = c;
        ptmpMsgHeader ->iCurrsequence = c ->iCurrsequence;
        pTmpBuffer = static_cast<char*>(pTmpBuffer) + m_iLenMsgHeader;                 //往后跳，跳过消息头，指向包头
        memcpy(pTmpBuffer, BHData, m_iLenPkgHeader); //直接把收到的包头拷贝进来

        if(e_pkgLen == m_iLenPkgHeader) //只有包头
        {
            floodflag = TestFlood(c);
            if(floodflag == true)
            {
                return;
            }
            ngx_wait_request_handler_proc_plast_ET(c);
            readflag = 1;
        }
        else
        {
            //开始收包体，注意写法
            c->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
            c->precvbuf = static_cast<char*>(pTmpBuffer) + m_iLenPkgHeader;  //pTmpBuffer指向包头，这里 + m_iLenPkgHeader后指向包体位置
            c->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体            
        }
    }    
}

void CSocket::ngx_wait_request_handler_proc_plast_ET(lpngx_connection_t c)
{
    // CLogicSocket::threadRecvProcFunc(c ->precvMemPointer);
    c ->logicObj ->threadRecvProcFunc(c ->precvMemPointer);
     //内存不再需要释放，因为你收完整了包，这个包被上边调用inMsgRecvQueue()移入消息队列，那么释放内存就属于业务逻辑去干，不需要回收连接到连接池中干了
    c->precvMemPointer  = nullptr;
    c ->curStat = _PKG_HD_INIT;     //数据处理完成后，又开始新一轮接收包头
    c ->precvbuf = c ->dataHeadInfo;
    c ->irecvlen = m_iLenPkgHeader; 
}

void CSocket::sendRemainMsg(void* arg)
{
    lpngx_connection_t pConn = static_cast<lpngx_connection_t>(arg);
    LPSTRUC_MSG_HEADER p_MsgHeader = reinterpret_cast<LPSTRUC_MSG_HEADER>(pConn ->psendMemPointer);
    CSocket* cs = CSocket::GetInstance();
    CMemory* mm = CMemory::GetInstance();
    Log* log = Log::GetInstance();
    int ret;
    // log ->ngx_log_stderr(0, "remain", pConn ->isendlen);
    if(p_MsgHeader ->iCurrsequence != pConn ->iCurrsequence)    //连接已断开
    {
        if(pConn ->deleteFlag)
        {
            mm ->FreeMemory(pConn ->psendMemPointer);
        }
        pConn ->psendMemPointer = nullptr;
        pConn ->psendbuf = nullptr;
        pConn ->isendlen = 0;
        pConn ->logicPorcMutex.unlock();
        log ->ngx_log_stderr(0, "connect has closed");
        return;
    }

    while(pConn ->isendlen != 0)
    {
        // ret = send(pConn ->fd, pConn ->psendbuf, pConn ->isendlen, 0);
        ret = pConn ->WriteFunc(pConn ->fd, pConn ->psendbuf, pConn ->isendlen, MSG_NOSIGNAL);
        if(ret < pConn ->isendlen && ret >= 0)
        {
            pConn ->psendbuf += ret;
            pConn ->isendlen -= ret;   
        }
        else if(ret == pConn ->isendlen)
        {
            cs ->ngx_epoll_opr_event(pConn, READEVENT, 0, EPOLL_CTL_MOD);   //发完加回去读事件
            pConn ->isendlen -= ret; 
            if(pConn ->deleteFlag)
            {
                mm ->FreeMemory(pConn ->psendMemPointer);
            }
            pConn ->psendMemPointer = nullptr;
            pConn ->psendbuf = nullptr;
            pConn ->isendlen = 0;
            pConn ->logicPorcMutex.unlock();
        }
        else if(ret == -1)
        {
            if(errno == EPIPE || errno == ECONNRESET)  //EPIPE：一次都没有发送对端就断开；ECONNRESET：发送成功过，但后续发送时对端断开了
            {
                if(pConn ->deleteFlag)
                {
                    mm ->FreeMemory(pConn ->psendMemPointer);
                }
                pConn ->psendMemPointer = nullptr;
                pConn ->psendbuf = nullptr;
                pConn ->isendlen = 0;
                pConn ->logicPorcMutex.unlock();
                break;
            }
        }
    }
}