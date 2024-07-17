#include <unistd.h>

#include "ngx_c_log.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_slogic.h"

void CSocket::ngx_close_connection(lpngx_connection_t c)//用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
{
    Log* log = Log::GetInstance();
    
    c ->FloodAttackCount = 0;   //重置flood攻击次数
    if(c -> fd != -1)
    {
        if(close(c -> fd) == -1)
        {
            log -> ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_accepted_connection()中close()失败!");  
        }
    }
    c -> fd = -1;

    ngx_free_connection(c);
}

lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
    std::unique_lock<std::mutex> ConLock(m_ConPoolMutex);
    lpngx_connection_t  c = m_pfree_connections;
    if(c == nullptr)
    {
        return nullptr;
    }
    if(c ->next == nullptr)
    {
        More_Connection_Pool(CREATECONNUM, c);
    }
    m_pfree_connections = c -> next;
    m_free_connection_n--;

    uintptr_t  instance = c->instance;   //常规c->instance在刚构造连接池时这里是1【失效】
    uint64_t iCurrsequence = c->iCurrsequence;
    //....其他内容再增加
    bzero(c, sizeof(ngx_connection_t));//(2)把以往有用的数据搞出来后，清空并给适当值!
    c -> fd = isock;
    //(3)这个值有用，所以在上边(1)中被保留，没有被清空，这里又把这个值赋回来
    c->instance = !instance;                            //抄自官方nginx，到底有啥用，以后再说【分配内存时候，连接池里每个连接对象这个变量给的值都为1，所以这里取反应该是0【有效】；】
    c->iCurrsequence=iCurrsequence;
    ++c->iCurrsequence;  //每次取用该值都增加1

    //wev->write = 1;  这个标记有没有 意义加，后续再研究
    return c;    
}

void CSocket::ngx_free_connection(lpngx_connection_t c) 
{
    CMemory* mm =CMemory::GetInstance();

    std::unique_lock<std::mutex> ConLock(m_ConPoolMutex);
    c -> next = m_pfree_connections;
    ++c->iCurrsequence;     //回收后，该值就增加1,以用于判断某些网络事件是否过期【一被释放就立即+1也是有必要的】
    m_pfree_connections = c;
    m_free_connection_n++;
    delete c ->logicObj;
}

bool CSocket::More_Connection_Pool(int ConNum, lpngx_connection_t c)
{
    int conNum = m_connection_n + ConNum;
    int CrateNum = 0;
    if(conNum <= m_connection_n_max)    //判断新创建连接数量，是否超过最大限制
    {
        CrateNum = ConNum;
    }
    else
    {
        CrateNum = m_connection_n_max - m_connection_n;
    }
    if(CrateNum == 0)   //达到最大数量，不新创建连接资源
    {
        return false;
    }
    lpngx_connection_t tptr = new ngx_connection_t[CrateNum];
    m_pconnections.push_back(tptr);
    c ->next = tptr;
    for(int i = 0; i < ConNum - 1; i++)
    {
        tptr -> next = tptr + 1;
        tptr -> fd = -1;                //初始化连接，无socket和该连接池中的连接【对象】绑定
        tptr -> instance = 1;           //失效标志位设置为1【失效】，此句抄自官方nginx，这句到底有啥用，后续再研究
        tptr -> iCurrsequence = 0;      //当前序号统一从0开始
        tptr++;        
    }
    tptr -> next = nullptr;
    m_connection_n += CrateNum;

    return true;
}

void CSocket::ngx_time_tofree_connection(void* arg)
{
    CSocket* cs = CSocket::GetInstance();
    
    std::unique_lock<std::mutex> FrLock(cs ->m_ConFreeMutex);
    cs ->m_recyconnectionList.push_back((lpngx_connection_t)arg);
    
    cs ->m_NotEmptyConn.notify_one();
}

void CSocket::ngx_timer_free_connection(void* arg)  //延时回收
{
    CSocket* cs = CSocket::GetInstance();

    auto condition = [=](void)->bool{
        if(cs ->m_recyconnectionList.size() != 0 || cs ->m_shutdown == true)   //当释放队列不为空，开始释放
            return true;
        else
            return false;
    };
    while(true)
    {
        std::unique_lock<std::mutex> FrLock(cs ->m_ConFreeMutex);
        cs ->m_NotEmptyConn.wait(FrLock, condition);
        if(cs ->m_shutdown == true)
        {
            return;
        }
        lpngx_connection_t con = cs ->m_recyconnectionList.front();
        cs ->m_recyconnectionList.pop_front();
        cs ->ngx_close_connection(con);  
        Log* log = Log::GetInstance();
        log ->ngx_log_stderr(0, con, "   ", cs ->m_pfree_connections, getpid());
    }
}

void CSocket::test(void* arg)           //
{
    CSocket* cs = CSocket::GetInstance();
    int t = cs ->m_onlineUserCount;

    Log* log = Log::GetInstance();
    log ->ngx_log_stderr(0, t, "   ", cs ->m_timerQueuemap.size(), "  ", cs ->m_connection_n);
}
