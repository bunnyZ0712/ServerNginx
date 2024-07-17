#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

#include "ngx_c_socket.h"
#include "ngx_c_log.h"
#include "ngx_c_memory.h"
#include "ngx_c_conf.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_timer.h"

CSocket* CSocket::m_instace = nullptr;

CSocket::CSocket()
{
    //配置相关
    m_worker_connections = 1;    //epoll连接最大项数
    m_ListenPortCount = 1;       //监听一个端口
    m_ETflag = 0;
    m_floodAkEnable = 0;

    //epoll相关
    m_epollhandle = -1;          //epoll返回的句柄
    // m_pconnections = NULL;       //连接池【连接数组】先给空
    m_pfree_connections = nullptr;  //连接池中空闲的连接链 
    //m_pread_events = NULL;       //读事件数组给空
    //m_pwrite_events = NULL;      //写事件数组给空
    m_shutdown = false;
    //一些和网络通讯有关的常用变量值，供后续频繁使用时提高效率
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);    //包头的sizeof值【占用的字节数】
    m_iLenMsgHeader =  sizeof(STRUC_MSG_HEADER);  //消息头的sizeof值【占用的字节数】
    m_sslctx = SSLCTXServer::GetInstance("server.crt", "server.key");
}

CSocket::~CSocket()
{
    m_shutdown = true;
    m_NotEmptyConn.notify_one();
    for(auto it : m_ListenSocketList)   //释放监听套接字资源
    {
        delete it;
    }
    m_ListenSocketList.clear();
    for(auto it : m_pconnections)   //释放连接池资源
    {
        delete[] it;
    }
    m_pconnections.clear();
    m_recyconnectionList.clear();
}

CSocket::Destroy_CS::~Destroy_CS()
{
    if(CSocket::m_instace != nullptr)
    {
        delete CSocket::m_instace;
        CSocket::m_instace = nullptr;
    }
}

void CSocket::Create(void)
{
    if(CSocket::m_instace == nullptr)
    {
        CSocket::m_instace = new CSocket;
        static Destroy_CS DCS;
    }
}

CSocket* CSocket::GetInstance(void)
{
    static std::once_flag create_flag;

    std::call_once(create_flag, Create);
    
    return CSocket::m_instace;
}

bool CSocket::Initialize()
{
    
    if(!ReadConf())
    {
        return false;
    }
    if(!ngx_open_listening_sockets())
    {
        return false;
    }

    return true;
}

bool CSocket::ReadConf()
{
    CConfig* cc = CConfig::GetInstance();
    Log* log = Log::GetInstance();

    if(!(cc -> GetNum<int>("ListenPortCount", m_ListenPortCount)))
    {
        log -> ngx_log_stderr(0, "Get ListenPortCount failed");
        return false;
    }
    if(!(cc -> GetNum<int>("worker_connections", m_worker_connections)))
    {
        log -> ngx_log_stderr(0, "Get worker_connections failed");
        return false;
    }
    cc -> GetNum<int>("ET_flag", m_ETflag);
    cc -> GetNum<int>("Flood_flag", m_floodAkEnable);
    cc -> GetNum<int>("FloodTimeInterval", m_floodTimeInterval);
    cc -> GetNum<int>("FloodKickCount", m_floodKickCount);
    m_connection_n_max = m_worker_connections + (m_worker_connections / 2);
    return true;
}

bool CSocket::ngx_open_listening_sockets()  //监听必须的端口【支持多个端口】
{
    CConfig* cc = CConfig::GetInstance();
    Log* log = Log::GetInstance();

    const char* server_ip;
    int server_port;
    int isock;
    sockaddr_in serveraddr;

    serveraddr.sin_family = AF_INET;
    server_ip = cc -> GetString("ServerIP");
    if(server_ip == nullptr)
    {
        serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        inet_pton(AF_INET, server_ip, &serveraddr.sin_addr.s_addr);
    }

    for(int i = 0; i < m_ListenPortCount; i++)
    {
        isock = socket(AF_INET, SOCK_STREAM, 0);
        if(isock == -1)
        {
            log -> ngx_log_stderr(errno, "socket failed");
            return false;
        }
        std::string portname = "ListenPort";
        portname += std::to_string(i);
        if(!(cc -> GetNum<int>(portname.c_str(), server_port)))
        {
            log -> ngx_log_stderr(0, portname.c_str(), "Get failed");
            close(isock);
            return false;
        }
        serveraddr.sin_port = htons(server_port);

        int reuseaddr = 1;  //1:打开对应的设置项
        if(setsockopt(isock,SOL_SOCKET, SO_REUSEADDR,(const void *) &reuseaddr, sizeof(reuseaddr)) == -1)   //避免TIME_WAIT
        {
            log -> ngx_log_stderr(errno, "CSocket::Initialize()中setsockopt(SO_REUSEADDR)失败,i=", i);
            close(isock); //无需理会是否正常执行了                                                  
            return false;
        }
        //设置该socket为非阻塞
        if(setnonblocking(isock) == false)
        {                
            log -> ngx_log_stderr(errno, "CSocket::Initialize()中setnonblocking()失败,i=", i);
            close(isock);
            return false;
        }

        if(bind(isock, (sockaddr*)&serveraddr, sizeof(sockaddr)) == -1)
        {
            log ->ngx_log_stderr(errno, portname.c_str(), "bind failed");
            close(isock);
            return false;
        }

        if(listen(isock, NGX_LISTEN_BACKLOG) == -1)
        {
            log ->ngx_log_stderr(errno, portname.c_str(), "listen failed");
            close(isock);
            return false;            
        }
        lpngx_listening_t p_listensocketitem = new ngx_listening_t;
        bzero(p_listensocketitem, sizeof(ngx_listening_t));
        p_listensocketitem -> fd = isock;                   
        p_listensocketitem -> port = server_port;
        log -> ngx_log_error_core(NGX_LOG_INFO, 0, "监听", server_port, "端口成功!");
        m_ListenSocketList.emplace_back(p_listensocketitem);
    }
    return true;
}

void CSocket::ngx_close_listening_sockets() //关闭监听套接字
{
    Log* log = Log::GetInstance();
    for(auto it : m_ListenSocketList)
    {
        close(it -> fd);
        log -> ngx_log_error_core(NGX_LOG_INFO, 0, "port:", it -> port, "close");
    }
}

bool CSocket::setnonblocking(int sockfd)    //设置非阻塞套接字
{
    Log* log = Log::GetInstance();
    int newsta, oldsta;
    oldsta = fcntl(sockfd, F_GETFL);
    if(oldsta == -1)
    {
        log -> ngx_log_stderr(errno, "fcntl(sockfd, F_GETFL) failed");
        return false;
    }

    newsta = oldsta | O_NONBLOCK;
    if(fcntl(sockfd, F_SETFL, newsta) == -1)
    {
        log -> ngx_log_stderr(errno, "fcntl(sockfd, F_SETFL, newsta) failed");
        return false;
    }
    return true;
}

int CSocket::ngx_epoll_init()
{
    Log* log = Log::GetInstance();
    CThread* ct = CThread::GetInstance();
    Timer* timer = Timer::GetInstance();
    m_epollhandle = epoll_create(m_worker_connections);
    if(m_epollhandle == -1)
    {
        log -> ngx_log_stderr(errno, "epoll_create failed");
        exit(2);     //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
    }
    // timer->AddTimer(20000, -1, std::bind(CSocket::test, std::placeholders::_1), this);   //测试，定时检查相关容器
    ct ->AddTask(std::bind(ngx_time_check, this));  //添加心跳包检测踢出线程
    ct ->AddTask(std::bind(ngx_timer_free_connection, this));//添加延时回收线程
    timer->AddTimer(20000, -1, std::bind(CSocket::ngx_time_tocheck, std::placeholders::_1), this);  //添加连接队列定时检测，定时进行心跳包检查
    //(2)创建连接池【数组】、创建出来，这个东西后续用于处理所有客户端的连接
    m_connection_n = m_worker_connections;      //记录当前连接池中连接总数
    lpngx_connection_t tptr = new ngx_connection_t[m_worker_connections];
    bzero(tptr, m_worker_connections* sizeof(ngx_connection_t));
    m_pconnections.push_back(tptr);
    m_pfree_connections = tptr;
    m_free_connection_n = m_worker_connections;

    for(int i = 0; i < m_worker_connections - 1; i++)   //初始化连接池
    {
        tptr -> next = tptr + 1;
        tptr -> fd = -1;                //初始化连接，无socket和该连接池中的连接【对象】绑定
        tptr -> instance = 1;           //失效标志位设置为1【失效】，此句抄自官方nginx，这句到底有啥用，后续再研究
        tptr -> iCurrsequence = 0;      //当前序号统一从0开始
        tptr++;
    }
    tptr -> next = nullptr;
    
    for(auto& it : m_ListenSocketList)  //将监听的端口加入epool
    {
        lpngx_connection_t t = ngx_get_connection(it -> fd);    //刚开始怎么会失败？
        if(t == nullptr)
        {
            log -> ngx_log_stderr(errno, "CSocket::ngx_epoll_init()中ngx_get_connection()失败.");
            exit(2);
        }

        it -> connection = t;
        t -> listening = it;

        t -> rhandler = &CSocket::ngx_event_accept;

        // if(ngx_epoll_add_event(it -> fd, 1, 0, 0, EPOLL_CTL_ADD, t) == -1)  //监听文件描述符不采用ET模式，要确保每个连接都可以被处理
        // {
        //     exit(2);
        // }
        epoll_event ev;
        ev.data.ptr = t;
        ev.events = EPOLLIN;
        if(epoll_ctl(m_epollhandle, EPOLL_CTL_ADD, t ->fd, &ev) == -1)
        {
            exit(2);
        }
    }

    return 1;
}

//开始获取发生的事件消息
//参数unsigned int timer：epoll_wait()阻塞的时长，单位是毫秒；
//返回值，1：正常返回  ,-1：有问题返回，0:定时器触发   一般不管是正常还是问题返回，都应该保持进程继续运行 
//本函数被ngx_process_events_and_timers()调用，而ngx_process_events_and_timers()是在子进程的死循环中被反复调用

int CSocket::ngx_epoll_process_events(int timer)    //epoll等待接收和处理事件
{
    Log* log = Log::GetInstance();
    int ret = epoll_wait(m_epollhandle, m_events, NGX_MAX_EVENTS, timer);

    lpngx_connection_t c;
    uintptr_t          instance;
    uint32_t           revents;
    // log ->ngx_log_stderr(0, "aaa", ret);
    if(ret == -1)
    {
        if(errno == EINTR)
        {
            log -> ngx_log_error_core(NGX_LOG_INFO, errno, "epoll_wait failed");
            return 1;
        }
        else
        {
            log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "epoll_wait failed");
            return -1;
        }
    }
    else if(ret == 0)
    {
        if(timer != -1) //计时到点
        {
            return 0;
        }
        log -> ngx_log_error_core(NGX_LOG_ALERT, errno, "epoll_wait failed");
        return -1;
    }

    for(int i = 0; i < ret; i++)
    {
        c = static_cast<lpngx_connection_t>(m_events[i].data.ptr);
        // instance = (uintptr_t) c & 1;  //将地址的最后一位取出来，用instance变量标识, 见ngx_epoll_add_event，该值是当时随着连接池中的连接一起给进来的
        // c = (lpngx_connection_t) ((uintptr_t)c & (uintptr_t) ~1); //最后1位干掉，得到真正的c地址

           //仔细分析一下官方nginx的这个判断
        // if(c->fd == -1)  //一个套接字，当关联一个 连接池中的连接【对象】时，这个套接字值是要给到c->fd的，
        //                    //那什么时候这个c->fd会变成-1呢？关闭连接时这个fd会被设置为-1，哪行代码设置的-1再研究，但应该不是ngx_free_connection()函数设置的-1
        // {                 
        //     //比如我们用epoll_wait取得三个事件，处理第一个事件时，因为业务需要，我们把这个连接关闭，那我们应该会把c->fd设置为-1；
        //     //第二个事件照常处理
        //     //第三个事件，假如这第三个事件，也跟第一个事件对应的是同一个连接，那这个条件就会成立；那么这种事件，属于过期事件，不该处理

        //     //这里可以增加个日志，也可以不增加日志
        //     log -> ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocket::ngx_epoll_process_events()中遇到了fd=-1的过期事件:", c); 
        //     continue; //这种事件就不处理即可
        // }

        // if(c->instance != instance)
        // {
        //     //--------------------以下这些说法来自于资料--------------------------------------
        //     //什么时候这个条件成立呢？【换种问法：instance标志为什么可以判断事件是否过期呢？】
        //     //比如我们用epoll_wait取得三个事件，处理第一个事件时，因为业务需要，我们把这个连接关闭【麻烦就麻烦在这个连接被服务器关闭上了】，但是恰好第三个事件也跟这个连接有关；
        //     //因为第一个事件就把socket连接关闭了，显然第三个事件我们是不应该处理的【因为这是个过期事件】，若处理肯定会导致错误；
        //     //那我们上述把c->fd设置为-1，可以解决这个问题吗？ 能解决一部分问题，但另外一部分不能解决，不能解决的问题描述如下【这么离奇的情况应该极少遇到】：
        //     //a)处理第一个事件时，因为业务需要，我们把这个连接【假设套接字为50】关闭，同时设置c->fd = -1;并且调用ngx_free_connection将该连接归还给连接池；
        //     //b)处理第二个事件，恰好第二个事件是建立新连接事件，调用ngx_get_connection从连接池中取出的连接非常可能就是刚刚释放的第一个事件对应的连接池中的连接；
        //     //c)又因为a中套接字50被释放了，所以会被操作系统拿来复用，复用给了b)【一般这么快就被复用也是醉了】；
        //     //d)当处理第三个事件时，第三个事件其实是已经过期的，应该不处理，那怎么判断这第三个事件是过期的呢？ 【假设现在处理的是第三个事件，此时这个 连接池中的该连接 实际上已经被用作第二个事件对应的socket上了】；
        //         //依靠instance标志位能够解决这个问题，当调用ngx_get_connection从连接池中获取一个新连接时，我们把instance标志位置反，所以这个条件如果不成立，说明这个连接已经被挪作他用了；

        //     //--------------------我的个人思考--------------------------------------
        //     //如果收到了若干个事件，其中连接关闭也搞了多次，导致这个instance标志位被取反2次，那么，造成的结果就是：还是有可能遇到某些过期事件没有被发现【这里也就没有被continue】，照旧被当做没过期事件处理了；
        //           //如果是这样，那就只能被照旧处理了。可能会造成偶尔某个连接被误关闭？但是整体服务器程序运行应该是平稳，问题不大的，这种漏网而被当成没过期来处理的的过期事件应该是极少发生的

        //     log -> ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocket::ngx_epoll_process_events()中遇到了instance值改变的过期事件:", c); 
        //     continue; //这种事件就不处理即可
        // } 
        revents = m_events[i].events;
        // if(revents &(EPOLLERR | EPOLLHUP))  //例如对方close掉套接字，这里会感应到【换句话说：如果发生了错误或者客户端断连】客户端正常断开连接
        //                                     //会返回EPOLLIN,此时读取数据会返回0
        // {
        //     //这加上读写标记，方便后续代码处理
        //     revents |= EPOLLIN|EPOLLOUT;   //EPOLLIN：表示对应的链接上有数据可以读出（TCP链接的远端主动关闭连接，也相当于可读事件，因为本服务器小处理发送来的FIN包）
        //                                    //EPOLLOUT：表示对应的连接上可以写入数据发送【写准备好】
        //     //ngx_log_stderr(errno,"2222222222222222222222222.");
        // } 
        if(revents &(EPOLLERR | EPOLLHUP))
        {
            (this->* (c->phandler) )(c);
        }
        if(revents == EPOLLIN)  //如果是读事件
        {
            //一个客户端新连入，这个会成立
            //c->r_ready = 1;               //标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】            
            (this->* (c->rhandler) )(c);    //注意括号的运用来正确设置优先级，防止编译出错；【如果是个新客户连入
                                              //如果新连接进入，这里执行的应该是CSocket::ngx_event_accept(c)】            
                                              //如果是已经连入，发送数据到这里，则这里执行的应该是 CSocket::ngx_wait_request_handler
            // log -> ngx_log_stderr(0, "read");
        }
        else if(revents == EPOLLOUT) //如果是写事件
        {
            //....待扩展
            (this->* (c->whandler) )(c);
            // log -> ngx_log_stderr(errno,"111111111111111111111111111111.", std::this_thread::get_id(), "  ", getpid());

        }
    }

    return 1;
}

//epoll增加事件，可能被ngx_epoll_init()等函数调用
//fd:句柄，一个socket
//readevent：表示是否是个读事件，0是，1不是
//writeevent：表示是否是个写事件，0是，1不是
//otherflag：其他需要额外补充的标记，弄到这里
//eventtype：事件类型  ，一般就是用系统的枚举值，增加，删除，修改等;
//c：对应的连接池中的连接的指针
//返回值：成功返回1，失败返回-1；

int CSocket::ngx_epoll_del_event(lpngx_connection_t c)
{
    Log* log = Log::GetInstance();
    if(epoll_ctl(m_epollhandle, EPOLL_CTL_DEL, c ->fd, nullptr) == -1)
    {
        log ->ngx_log_error_core(NGX_LOG_ALERT, errno, "epoll_ctl delete event failed");
        return -1;
    }

    return 1;
}

bool CSocket::ngx_epoll_opr_event(lpngx_connection_t c, int event, uint32_t otherflag, uint32_t eventtype)
{
    Log* log = Log::GetInstance();
    epoll_event ev;

    if(event == READEVENT)
    {
        ev.events = EPOLLIN;
    }
    else
    {
        ev.events = EPOLLOUT | EPOLLET;
    }
    if(otherflag != 0)
    {
        ev.events |= otherflag;
    }

    if(m_ETflag == ETOPEN)
    {
        c ->rhandler = &CSocket::ngx_read_request_handler_ET;
        ev.events |= EPOLLET;
    }
    else
    {
        c ->rhandler = &CSocket::ngx_read_request_handler;
    }
    ev.data.ptr = c;

    if(epoll_ctl(m_epollhandle, eventtype, c ->fd, &ev) == -1)
    {
        return false;
    }
    return true;
}

bool CSocket::TestFlood(lpngx_connection_t pConn)
{
    Log* log = Log::GetInstance();
    timeval Curtime;
    gettimeofday(&Curtime, nullptr);
    if(m_ETflag == ETCLOSE)
    {
        pConn ->logicPorcMutex.lock();
    }
    auto TimeNow = Curtime.tv_sec * 1000 + Curtime.tv_usec / 1000;

    // log ->ngx_log_stderr(0, TimeNow - pConn ->FloodkickLastTime);
    if(TimeNow - pConn ->FloodkickLastTime < m_floodTimeInterval)   //频率小于最小要求，则计数加一
    {
        pConn->FloodAttackCount++;
        pConn->FloodkickLastTime = TimeNow;
    }
    else    //频率不小于最小，重置次数
    {
        pConn->FloodAttackCount = 0;
        pConn->FloodkickLastTime = TimeNow;
    }
    // log ->ngx_log_stderr(0, pConn->FloodAttackCount, "   ", m_floodKickCount);
    if(pConn->FloodAttackCount == m_floodKickCount)     //累计次数达到阈值返回true，进行踢出
    {
        if(m_ETflag == ETCLOSE)
        {
            pConn ->logicPorcMutex.unlock();
        }
        return true;    //达到泛洪次数就踢出
    }
    if(m_ETflag == ETCLOSE)
    {
        pConn ->logicPorcMutex.unlock();
    }
    return false;
}

int CSocket::GetETFlag(void)
{
    return m_ETflag;
}

void CSocket::ngx_evhup_handler(lpngx_connection_t c)   //断开连接回调函数
{
    Log* log = Log::GetInstance();
    char t[2];
    recv(c ->fd, t, 2, 0);
    log -> ngx_log_stderr(errno," close");
    Timer* timer = Timer::GetInstance();
    u_char caddr[NGX_IPLENGTH_MAX];
    ngx_sock_ntop(&c ->s_sockaddr, 1, caddr, 0);
 
    ngx_epoll_del_event(c); //删除监听事件
    m_onlineUserCount--;
    timer->AddTimer(10000, 1, std::bind(CSocket::ngx_time_tofree_connection, std::placeholders::_1), c);
    std::unique_lock<std::mutex> PingLock(m_PingMutex);
    m_timerQueuemap.erase(m_timerQueuemap.find(c ->storePingTime));    
}
