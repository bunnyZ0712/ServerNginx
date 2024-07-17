#include <unistd.h>
#include <thread>

#include "ngx_c_slogic.h"
#include "ngx_c_memory.h"
#include "ngx_c_log.h"
#include "ngx_c_crc32.h"
#include "ngx_comm.h"

// std::map<int, std::function<bool(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)>> CLogicSocket::FuncBuf;
// int CLogicSocket::HandleNum;

CLogicSocket::CLogicSocket()
{
    FuncBuf.emplace(_CMD_PING, std::bind(&CLogicSocket::_HandlePing, this, std::placeholders::_1, std::placeholders::_2,std::placeholders::_3,std::placeholders::_4));
    FuncBuf.emplace(_CMD_BUSINESS, std::bind(&CLogicSocket::_HandleBusiness, this, std::placeholders::_1, std::placeholders::_2,std::placeholders::_3,std::placeholders::_4));
    // FuncBuf.emplace(2, std::bind(CLogicSocket::_HandleLogIn, std::placeholders::_1, std::placeholders::_2,std::placeholders::_3,std::placeholders::_4));

    HandleNum = FuncBuf.size();
}

//pMsgBuf:整个消息
void CLogicSocket::threadRecvProcFunc(void* pMsgBuf)
{
    CMemory* mm = CMemory::GetInstance();
    Log* log = Log::GetInstance();
    char* tptr = static_cast<char*>(pMsgBuf);
    LPSTRUC_MSG_HEADER pMsgHeader = reinterpret_cast<LPSTRUC_MSG_HEADER>(tptr); //消息头
    LPCOMM_PKG_HEADER  pPkgHeader = reinterpret_cast<LPCOMM_PKG_HEADER>(tptr + MSGHEADERLEN);//包头
    void* pPkgBody = nullptr;
    unsigned short pkgLen = pPkgHeader ->pkgLen;

    if(pkgLen == PKGHEADERLEN)   //只有包头无包体数据
    {
        if(pPkgHeader ->msgCode != _CMD_PING)   //只有包体，crc为0，只有ping包没有主体数据
        {
            return;
        }
        pPkgBody = nullptr;
    }
    // else    //有包体数据，验证crc
    // {
    //     pPkgBody = tptr + MSGHEADERLEN + PKGHEADERLEN;
    //     CCRC32* crc = CCRC32::GetInstance();
    //     int crcval = crc ->Get_CRC((unsigned char *)pPkgBody, pkgLen - PKGHEADERLEN);
    //     if(crcval != pPkgHeader->crc32)
    //     {
    //         log ->ngx_log_stderr(0, "crc error");
    //         return;
    //     }
    // }
    if(pMsgHeader ->iCurrsequence != pMsgHeader ->pConn ->iCurrsequence)    //判断事件是否过期
    {
        log ->ngx_log_stderr(0, "event timeout");
        return;
    }
    pPkgBody = tptr + MSGHEADERLEN + PKGHEADERLEN;
    lpngx_connection_t pConn = pMsgHeader ->pConn;
    unsigned short msgCode = pPkgHeader ->msgCode;

    // log ->ngx_log_stderr(0, msgCode);
    if(msgCode >= HandleNum)    //判断消息码是否正确，不应超过业务总数
    {
        log ->ngx_log_stderr(0, "msgCode oversize");
        return;        
    }
    auto it = FuncBuf.find(msgCode);    //根据消息码判断调用对应数据处理函数
    it ->second(pConn, pMsgHeader, (char *)pPkgBody, pkgLen - PKGHEADERLEN);

    mm ->FreeMemory(pMsgBuf);   //释放消息 消息头+包头+数据
}

//pConn::连接池中指针
//pMsgHeader::消息头指针
//pPkgBody::包体数据指针
//iBodyLength::包体数据长度
bool CLogicSocket::_HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    CMemory* mm = CMemory::GetInstance();
    CSocket* cs = CSocket::GetInstance();
    Log* log = Log::GetInstance();
    if(iBodyLength != 0)    //心跳包应该没有数据部分
    {
        return false;
    }
    // std::unique_lock<std::mutex> UserLock(pConn ->logicPorcMutex);
    if(cs ->GetETFlag() == ETCLOSE) //LT模式才加锁，ET模式在读取数据时就已经加锁
    {
        pConn ->logicPorcMutex.lock();  //做完业务处理，发送完成之后解锁
    }
    pConn ->lastPingTime = time(NULL);
    char* tptr = (char*)mm ->AllocMemory(MSGHEADERLEN + PKGHEADERLEN, false);   //申请发送消息内存
    LPSTRUC_MSG_HEADER p_Msgheader = reinterpret_cast<LPSTRUC_MSG_HEADER>(tptr);
    //填充消息头
    memcpy(tptr, pMsgHeader, MSGHEADERLEN + PKGHEADERLEN);
    //填充包头
    LPCOMM_PKG_HEADER p_PKGInfo = reinterpret_cast<LPCOMM_PKG_HEADER>(tptr + MSGHEADERLEN);
    p_Msgheader ->pConn ->psendMemPointer = tptr;
    p_Msgheader ->pConn ->psendbuf = (char*)p_PKGInfo;
    p_PKGInfo ->pkgLen = PKGHEADERLEN;
    p_PKGInfo ->msgCode = _CMD_PING;
    p_PKGInfo ->crc32 = 0;
    p_Msgheader ->pConn ->isendlen = PKGHEADERLEN;
    pConn ->deleteFlag = true;
    log ->ngx_log_stderr(0, "ping");

    cs ->sendMsg(p_Msgheader);

    return true;
}

bool CLogicSocket::_HandleBusiness(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    CSocket* cs = CSocket::GetInstance();
    Log* log = Log::GetInstance();
    if(cs ->GetETFlag() == ETCLOSE)  //LT模式才加锁，ET模式在读取数据时就已经加锁
    {
        pConn ->logicPorcMutex.lock();  //做完业务处理，发送完成之后解锁
    }

    Business(pConn, pMsgHeader, pPkgBody, iBodyLength, pConn ->deleteFlag);
    cs ->sendMsg(pMsgHeader);   //这里因为没有申请发送空间，所以用之前接收的就可以

    return true;
}

// bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
// {
//     CSocket* cs = CSocket::GetInstance();
//     Log* log = Log::GetInstance();
//     //(1)首先判断包体的合法性
//     int DataLen = sizeof(STRUCT_REGISTER);
//     if(iBodyLength != DataLen)  
//     {
//         return false;
//     }
//     else if(pPkgBody == nullptr)
//     {
//         return false;
//     }
//     //有时用户会在上一条业务还未处理完成发来第二条数据，为避免业务之间冲突，所以加锁保护，保证其业务计算正确
//     if(cs ->GetETFlag() == ETCLOSE)
//     {
//         pConn ->logicPorcMutex.lock();  //做完业务处理，发送完成之后解锁
//     }
//     LPSTRUCT_REGISTER p_RecvInfo = reinterpret_cast<LPSTRUCT_REGISTER>(pPkgBody);
//     //(4)这里可能要考虑 根据业务逻辑，进一步判断收到的数据的合法性，
//        //当前该玩家的状态是否适合收到这个数据等等【比如如果用户没登陆，它就不适合购买物品等等】
//         //这里大家自己发挥，自己根据业务需要来扩充代码，老师就不带着大家扩充了。。。。。。。。。。。。
//     //。。。。。。。。
//     //给客户端回信
//     CMemory* mm = CMemory::GetInstance();
//     CCRC32* crc = CCRC32::GetInstance();
//     int iSendLen = sizeof(STRUCT_REGISTER);
//     char* tptr = (char*)mm ->AllocMemory(MSGHEADERLEN + PKGHEADERLEN + sizeof(STRUCT_REGISTER), false);//分配发送缓冲区
//     LPSTRUC_MSG_HEADER p_Msgheader = reinterpret_cast<LPSTRUC_MSG_HEADER>(tptr);
//     //填充消息头
//     memcpy(tptr, pMsgHeader, MSGHEADERLEN);
//     p_Msgheader ->pConn ->psendMemPointer = tptr;
//     //填充包头
//     LPCOMM_PKG_HEADER p_PKGInfo = reinterpret_cast<LPCOMM_PKG_HEADER>(tptr + MSGHEADERLEN);
//     p_Msgheader ->pConn ->psendbuf = (char*)p_PKGInfo;
//     p_PKGInfo ->pkgLen = PKGHEADERLEN + iSendLen;
//     p_PKGInfo ->msgCode = _CMD_REGISTER;
//     //填充包体
//     LPSTRUCT_REGISTER p_SendInfo = reinterpret_cast<LPSTRUCT_REGISTER>(tptr + MSGHEADERLEN + PKGHEADERLEN);
//     memcpy(p_SendInfo, pPkgBody, iSendLen);
//     p_PKGInfo ->crc32 = crc ->Get_CRC((unsigned char*)p_SendInfo, iSendLen);
//     p_Msgheader ->pConn ->isendlen = iSendLen;
//     //发送数据

//     cs ->sendMsg(p_Msgheader);

    
//     return true;
// }

// bool CLogicSocket::_HandleLogIn(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
// {
//     Log* log = Log::GetInstance();
//     log ->ngx_log_stderr(0,"执行了CLogicSocket::_HandleLogIn()!", std::this_thread::get_id());
    
//     return true;
// }