#include <unistd.h>

#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_log.h"
#include "ngx_c_timer.h"

void CSocket::ngx_add_check(lpngx_connection_t c)   //当有连接接入，向map容器添加该链接，用于心跳包检测
{
    CMemory *p_memory = CMemory::GetInstance();
    c ->lastPingTime = time(NULL);
    c ->storePingTime = c ->lastPingTime;
    std::unique_lock<std::mutex> CHLock(m_PingMutex);
    LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader,false);
    tmpMsgHeader->pConn = c;
    tmpMsgHeader->iCurrsequence = c->iCurrsequence;
    m_timerQueuemap.emplace(c ->lastPingTime, tmpMsgHeader);
}

void CSocket::ngx_free_con_self(lpngx_connection_t c)//断开连接，删除epoll监听，延时释放连接池资源
{
    Log* log = Log::GetInstance();
    Timer* timer = Timer::GetInstance();
    ngx_epoll_del_event(c); //删除监听事件
    if(c ->fd != -1)    //断开连接，后续延时释放连接池资源
    {
        close(c ->fd);
        c ->fd = -1;
    }
    m_onlineUserCount--;
    timer->AddTimer(30000, 1, std::bind(CSocket::ngx_time_tofree_connection, std::placeholders::_1), c);//添加延时回收
    std::unique_lock<std::mutex> PingLock(m_PingMutex);
    m_timerQueuemap.erase(m_timerQueuemap.find(c ->storePingTime));
}

//arg:lpngx_connection_t
void CSocket::ngx_time_tocheck(void* arg)   //定时唤醒心跳包检测线程
{
    CSocket* cs = static_cast<CSocket*>(arg);

    cs ->m_NotEmptyPing.notify_one();    
}

void CSocket::ngx_time_check(void* arg) //心跳包延时检测处理
{
    CSocket* cs = static_cast<CSocket*>(arg);
    CMemory *mm = CMemory::GetInstance();
    lpngx_connection_t c = static_cast<lpngx_connection_t>(arg);
    Log* log = Log::GetInstance();

    auto condition = [=](void)->bool{               //判断是否有连接，有才进行心跳包检测
        if(cs ->m_shutdown == true || cs ->m_timerQueuemap.size() != 0)
            return true;
        else
            return false;
    };
    while(true)
    {
        std::unique_lock<std::mutex> CHLock(cs ->m_PingMutex);  //如果没有连接该线程不工作
        cs ->m_NotEmptyPing.wait(CHLock, condition);
        if(cs ->m_shutdown == true)         //如果是关闭，直接结束
        {
            return;
        }
        sleep(1);
        time_t now = time(NULL);    //返回时间单位为秒
        if((now - cs ->m_timerQueuemap.begin() ->first) < WAITTIME * 3 + 10)    //  如果第一个连接的间隔都小于要求，则不用判断后面的
        {
            continue;
        }
        //log ->ngx_log_stderr(0, "now", now);
        for(auto it = cs ->m_timerQueuemap.begin(); it != cs ->m_timerQueuemap.end();)
        {
            if(it ->second ->iCurrsequence != it ->second ->pConn ->iCurrsequence)  //已经断开连接
            {
                mm ->FreeMemory(it ->second);
                it = cs ->m_timerQueuemap.erase(it);
                continue;
            }
            else
            {
                if((now - it ->first)>= WAITTIME * 3 + 10)      //超过检测间隔，则踢出
                {
                    //log ->ngx_log_stderr(0, "aaa", it ->first, "  ", it ->second ->pConn ->lastPingTime, "  ", now - it ->second ->pConn ->lastPingTime);
                    if(it ->first == it ->second ->pConn ->lastPingTime)
                    {
                        cs ->ngx_free_con_self(it ->second ->pConn); //断开连接，删除epoll监听，延时释放连接池资源
                        mm ->FreeMemory(it ->second);   //释放消息头资源
                        it = cs ->m_timerQueuemap.erase(it);
                        continue;
                    }
                    else if(it ->first != it ->second ->pConn ->lastPingTime && (now - it ->second ->pConn ->lastPingTime) >= WAITTIME * 3 + 10)//更新时间后还超时
                    {
                        cs ->ngx_free_con_self(it ->second ->pConn); //断开连接，删除epoll监听，延时释放连接池资源
                        mm ->FreeMemory(it ->second);   //释放消息头资源
                        it = cs ->m_timerQueuemap.erase(it);
                        //log ->ngx_log_stderr(0, "bbb");
                        continue;
                    }
                    else if(it ->first != it ->second ->pConn ->lastPingTime && (now - it ->second ->pConn ->lastPingTime) < WAITTIME * 3 + 10)//更新时间后没有超时
                    {
                        std::pair<int, LPSTRUC_MSG_HEADER> t = *it;
                        t.first = t.second->pConn->lastPingTime;
                        t.second->pConn->storePingTime = t.second->pConn->lastPingTime;
                        cs ->m_timerQueuemap.erase(it);         //重新将其加入检测队列
                        cs ->m_timerQueuemap.insert(t);
                        it = cs ->m_timerQueuemap.begin();
                        //log ->ngx_log_stderr(0, "ccc");
                        continue;
                    }
                }
                else if((now - it ->second ->pConn ->lastPingTime)<= WAITTIME * 3 + 10) //找到第一个未超时的，后续的肯定都没超时
                {
                    break;
                }
            }
        }
    }
}