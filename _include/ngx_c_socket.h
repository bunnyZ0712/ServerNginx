#ifndef NGX_C_SOCKET_H
#define NGX_C_SOCKET_H

#include <vector>
#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <list>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>

#include "ngx_comm.h"
#include "ngx_c_sslctx.h"

#define NGX_LISTEN_BACKLOG  511    //已完成连接队列，nginx给511，我们也先按照这个来：不懂这个数字的同学参考第五章第四节
#define NGX_MAX_EVENTS      512    //epoll_wait一次最多接收这么多个事件，nginx中缺省是512，我们这里固定给成512就行，没太大必要修改
#define NGX_IPLENGTH_MAX    24
#define MSGHEADERLEN 		sizeof(_STRUC_MSG_HEADER)	//消息头长度
#define PKGHEADERLEN 		sizeof(COMM_PKG_HEADER)		//包头长度
#define CREATECONNUM		16		//连接池新增数量
#define READEVENT			1		//读写标志
#define WRITEEVENT			0
#define ETOPEN				1		//ET开启标志
#define ETCLOSE				0
#define WAITTIME			20		//心跳包等待时间 WAITTIME * 3 + 10,WAITTIME时间触发一次检测
#define FLOODOPEN			1		//flood攻击开启标志
#define FLOODCLOSE			0

typedef struct ngx_listening_s   ngx_listening_t, *lpngx_listening_t;
typedef struct ngx_connection_s  ngx_connection_t,*lpngx_connection_t;
typedef class  CSocket           CSocket;
class CLogicSocket;
typedef void (CSocket::*ngx_event_handler_pt)(lpngx_connection_t c); //定义成员函数指针
//一些专用结构定义放在这里，暂时不考虑放ngx_global.h里了-------------------------
struct ngx_listening_s  //和监听端口有关的结构
{
	int                       port;        //监听的端口号
	int                       fd;          //套接字句柄socket
	lpngx_connection_t        connection;  //连接池中的一个连接，注意这是个指针 
};

//以下三个结构是非常重要的三个结构，我们遵从官方nginx的写法；
//(1)该结构表示一个TCP连接【客户端主动发起的、Nginx服务器被动接受的TCP连接】
struct ngx_connection_s
{
	
	int                       fd;             //套接字句柄socket
	lpngx_listening_t         listening;      //如果这个链接被分配给了一个监听套接字，那么这个里边就指向监听套接字对应的那个lpngx_listening_t的内存首地址		

	//------------------------------------	
	unsigned                  instance:1;     //【位域】失效标志位：0：有效，1：失效【这个是官方nginx提供，到底有什么用，ngx_epoll_process_events()中详解】  
	uint64_t                  iCurrsequence;  //我引入的一个序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包，具体怎么用，用到了再说
	struct sockaddr           s_sockaddr;     //保存对方地址信息用的

	ngx_event_handler_pt      rhandler;       //读事件的相关处理方法
	ngx_event_handler_pt      whandler;       //写事件的相关处理方法
	ngx_event_handler_pt      phandler;       //关闭socket事件的相关处理方法

	//读写函数
	std::function<int(int, void*, size_t, int)> 	  ReadFunc;
	std::function<int(int, const void*, size_t, int)> WriteFunc;
	CLogicSocket*			  logicObj;							//业务类
	//和epoll事件有关
	uint32_t                  events;                         //和epoll事件有关 

	//收包相关
	unsigned char             curStat;                        //当前收包的状态
	char                      dataHeadInfo[_DATA_BUFSIZE_];   //用于保存收到的数据的包头信息			
	char                      *precvbuf;                      //接收数据的缓冲区的头指针，对收到不全的包非常有用，看具体应用的代码
	unsigned int              irecvlen;                       //要收到多少数据，由这个变量指定，和precvbuf配套使用，看具体应用的代码
	char                      *precvMemPointer;                //new出来的用于收包的内存首地址，和ifnewrecvMem配对使用消息头 + 包头 + 包体

	std::mutex 				  logicPorcMutex;                 //逻辑处理相关的互斥量
	time_t                    lastPingTime;					  //心跳包时间
	time_t 					  storePingTime;				  //存储的时间

	//和发包有关连接资源
	bool					  deleteFlag;					  //是否释放发送缓冲区标志
	char                      *psendMemPointer;               //发送完成后释放用的，整个数据的头指针，其实是 消息头 + 包头 + 包体
	char                      *psendbuf;                      //发送数据的缓冲区的头指针，其实是包头+包体
	unsigned int              isendlen;                       //要发送多少数据

	//和网络安全有关	
	uint64_t                  FloodkickLastTime;              //Flood攻击上次收到包的时间
	int                       FloodAttackCount;               //Flood攻击在该时间内收到包的次数统计

	//--------------------------------------------------
	lpngx_connection_t        next;           //指向下一个空闲的连接资源
};

//消息头，引入的目的是当收到数据包时，额外记录一些内容以备将来使用
typedef struct _STRUC_MSG_HEADER
{
	lpngx_connection_t pConn;         //记录对应的链接，注意这是个指针
	uint64_t           iCurrsequence; //收到数据包时记录对应连接的序号，将来能用于比较是否连接已经作废用
	//......其他以后扩展	
}STRUC_MSG_HEADER,*LPSTRUC_MSG_HEADER;
//整个消息队列数据格式：消息头+包头+包体（数据）

//socket相关类
class CSocket{
private:
	CSocket();                                    //构造函数
	~CSocket();                                   //释放函数
private:
	static CSocket* m_instace;
	class Destroy_CS{
	public:
		~Destroy_CS();
	};
	static void Create(void);
public:	
	static CSocket* GetInstance(void);
	int  ngx_epoll_init();                                       //epoll功能初始化，初始化连接池，将监听文件描述符加入epoll监听，worker进程中执行
	int  ngx_epoll_process_events(int timer);                   //epoll等待接收和处理事
	bool Initialize();                            			   //初始化函数，读取配置文件相关参数，开启TCP连接，master进程中执行
	void sendMsg(void* arg);										//发送数据
	int GetETFlag(void);
private:
    bool ReadConf();                                       //专门用于读各种配置项
	bool ngx_open_listening_sockets();                    //监听必须的端口【支持多个端口】
	void ngx_close_listening_sockets();                   //关闭监听套接字
	bool setnonblocking(int sockfd);                      //设置非阻塞套接字
	bool ngx_epoll_opr_event(lpngx_connection_t c, int event, uint32_t otherflag, uint32_t eventtype);	//设置epoll事件
	int  ngx_epoll_del_event(lpngx_connection_t c);		  //删除epoll监听事件
	bool More_Connection_Pool(int ConNum, lpngx_connection_t c);	//创建新的连接池资源,连接池用完的时候才会调用
	//一些业务处理函数handler
	void ngx_event_accept(lpngx_connection_t oldc);                    //建立新连接处理函数
	void ngx_evhup_handler(lpngx_connection_t c);						//关闭socket处理函数
	void ngx_read_request_handler(lpngx_connection_t c);               //默认设置数据来时的读处理函数LT模式
	void ngx_write_request_handler(lpngx_connection_t c);             //设置数据发送时的写处理函数
	void ngx_read_request_handler_ET(lpngx_connection_t c);				//设置数据来时的读处理函数ET模式，使用线程读取
	void ngx_close_connection(lpngx_connection_t c);          			//对端关闭之后，关闭连接处理函数,关闭套接字，立即释放连接资源
	static void recvproc_ET(void* arg);									//ET模式读取数据函数
	ssize_t recvproc(lpngx_connection_t c,char *buff,ssize_t buflen);  //接收从客户端来的数据专用函数
	void ngx_wait_request_handler_proc_p1(lpngx_connection_t c, bool& floodflag);       //包头收完整后的处理，配置后续数据读取相关参数，如果只有包头，则直接进行处理	  
	void ngx_wait_request_handler_proc_p1_ET(lpngx_connection_t c, int& readflag, bool& floodflag);		// 包头收完整后的处理，配置后续数据读取相关参数，如果只有包头，则直接进行处理，ET模式                                                                
	void ngx_wait_request_handler_proc_plast(lpngx_connection_t c);    //收到一个完整包后的处理，业务处理
	void ngx_wait_request_handler_proc_plast_ET(lpngx_connection_t c);    //收到一个完整包后的处理，业务处理ET模式
	static void sendRemainMsg(void* arg);						//发送未发送完成的任务
	//获取对端信息相关                                              
	size_t ngx_sock_ntop(struct sockaddr *sa,int port,u_char *text,size_t len);  //根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度
	//连接池或连接相关
	lpngx_connection_t ngx_get_connection(int isock);                  //从连接池中获取一个空闲连接
	void ngx_free_connection(lpngx_connection_t c);                    //归还参数c所代表的连接到到连接池中
	void ngx_add_check(lpngx_connection_t c);							//有新连接接入时，添加心跳包检测	
	void ngx_free_con_self(lpngx_connection_t c);						//心跳包超时，//断开连接，删除epoll监听，延时释放连接池资源

	static void ngx_time_tofree_connection(void* arg);					//添加定时器，定时回收连接池资源	到点再放进资源池
	static void ngx_timer_free_connection(void* arg);					//定时回收连接池线程
	static void ngx_time_tocheck(void* arg);							//添加定时器定时检查超时	
	static void ngx_time_check(void* arg);								//检查超时		
	//检查是否超时，中间会改变状态，所以不能像延时释放一样，需要将连接放入容器中，固定时间去检查，当有超时的就踢出
	//和网络安全有关
	bool TestFlood(lpngx_connection_t pConn);    //测试是否flood攻击成立，成立则返回true，否则返回false
	static void test(void* arg);
private:
    int                            m_worker_connections;               //epoll连接的最大项数
	int                            m_ListenPortCount;     //所监听的端口数量
	std::vector<lpngx_listening_t> m_ListenSocketList;    //监听套接字队列
	int                            m_epollhandle;                      //epoll_create返回的句柄

	//和连接池有关的
	std::list<lpngx_connection_t>  m_pconnections;                     //注意这里可是个指针，其实这是个连接池的首地址
	std::list<lpngx_connection_t>  m_recyconnectionList;                  //空闲连接列表【这里边装的全是空闲的连接】
	lpngx_connection_t             m_pfree_connections;                //空闲连接链表头，连接池中总是有某些连接被占用，为了快速在池中找到一个空闲的连接，我把空闲的连接专门用该成员记录;
	int							   m_RecyConnectionWaitTime;              //等待这么些秒后才回收连接
	                            
	int                            m_connection_n;                     //当前进程中所有连接对象的总数【连接池大小】
	int                            m_free_connection_n;                //连接池中可用连接总数
	int 						   m_connection_n_max;					//连接池最大数量
	struct epoll_event             m_events[NGX_MAX_EVENTS];           //用于在epoll_wait()中承载返回的所发生的事件

	//一些和网络通讯有关的成员变量
	size_t                         m_iLenPkgHeader;                    //sizeof(COMM_PKG_HEADER);		
	size_t                         m_iLenMsgHeader;                    //sizeof(STRUC_MSG_HEADER);

	std::multimap<time_t, LPSTRUC_MSG_HEADER>	m_timerQueuemap;	//心跳包队列
	std::mutex					   m_ConFreeMutex;            //延时回收锁
	std::mutex					   m_ConPoolMutex;				//连接池锁
	std::condition_variable		   m_NotEmptyConn;			  //延时回收条件变量
	std::mutex 					   m_PingMutex;					//心跳包锁
	std::condition_variable		   m_NotEmptyPing;			  //心跳包条件变量
	bool 						   m_shutdown;				  //退出延时回收线程
	int						       m_ETflag;					//ET模式开启标志
	//在线用户相关
	std::atomic<int>               m_onlineUserCount;                     //当前在线用户数统计
	//网络安全相关
	int                            m_floodAkEnable;                       //Flood攻击检测是否开启,1：开启   0：不开启
	int                   		   m_floodTimeInterval;                   //表示每次收到数据包的时间间隔是100(毫秒)
	int                            m_floodKickCount;                      //累积多少次踢出此人

	//TLS相关
	std::shared_ptr<SSLCTXServer>  m_sslctx;
};

#endif