#ifndef NGX_C_HTTPS
#define NGX_C_HTTPS

#include "ngx_c_http.h"
#include "ngx_c_sslctx.h"
#include <memory>
#include <iostream>

class HttpsServer : public HttpServer{
public:
    HttpsServer(int sockFd);
    ~HttpsServer();
    // void threadRecvProcFunc(void* pMsgBuf) override;
    //留给外部调用的读写接口
	virtual int ReadData(int fd, void* dataBuf, size_t length, int flag) override;  
	virtual int WriteData(int fd, const void* dataBuf, size_t iovCount, int flag) override;  
    MYSSL* GetSSL(void);  
protected:
    virtual bool Business(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength, bool& deleteFlag) override;
    int HttpsReadData(int fd, void* dataBuf, size_t length, int flag);
    int HttpsWriteData(int fd, const void* dataBuf, size_t iovCount, int flag);
protected:
    std::shared_ptr<SSLCTXServer> mSslctxServer;
    MYSSL* mSSL;
};


#endif