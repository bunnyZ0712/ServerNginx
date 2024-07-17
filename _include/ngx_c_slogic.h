#ifndef NGX_C_LOGIC_H
#define NGX_C_LOGIC_H

#include <map>
#include <functional>
#include "ngx_c_socket.h"

class CLogicSocket{
public:
    CLogicSocket();
    void threadRecvProcFunc(void* pMsgBuf);			//线程池回调函数,预处理数据，判断校验，调用对应业务函数
    virtual int ReadData(int fd, void* dataBuf, size_t length, int flag) = 0;   //每个协议对应的读写函数不一致，所以需要封装
	virtual int WriteData(int fd, const void* dataBuf, size_t iovCount, int flag) = 0;
protected:
	//各种业务逻辑相关函数都在之类
    bool _HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength); //心跳包检测
    bool _HandleBusiness(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
    virtual bool Business(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength, bool& deleteFlag) = 0; //业务处理
private:
    std::map<int, std::function<bool(lpngx_connection_t,LPSTRUC_MSG_HEADER,char*,unsigned short)>> FuncBuf;
    int HandleNum;       //业务数量
};


#endif