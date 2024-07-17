#include "ngx_c_https.h"
#include <sys/mman.h>

HttpsServer::HttpsServer(int sockFd)
{
    mSslctxServer = SSLCTXServer::GetInstance();
    mSSL = mSslctxServer ->NewSSL(sockFd);
    this ->m_sockfd = sockFd;
}

HttpsServer::~HttpsServer()
{
    delete mSSL;
}

MYSSL* HttpsServer::GetSSL(void)
{
    return mSSL;
}

int HttpsServer::ReadData(int fd, void* dataBuf, size_t length, int flag)
{
    return HttpsReadData(fd, dataBuf, length, flag);
}

int HttpsServer::WriteData(int fd, const void* dataBuf, size_t iovCount, int flag)
{
    return HttpsWriteData(fd, dataBuf, iovCount, flag);
}

int HttpsServer::HttpsWriteData(int fd, const void* dataBuf, size_t iovCount, int flag)
{   
    int count = 0;

    for(int i = 0; i < m_iov_count; i++)
    {
        count += mSSL ->Write(m_iov[i].iov_base, m_iov[i].iov_len);
    }
    if(m_file_ptr != nullptr)
    {
        munmap(m_file_ptr, m_stat.st_size);
    }    
    return count;
}

int HttpsServer::HttpsReadData(int fd, void* dataBuf, size_t length, int flag)
{
    return mSSL ->Read(dataBuf, length);
}

bool HttpsServer::Business(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength, bool& deleteFlag)
{
    char* tptr = reinterpret_cast<char*>(pMsgHeader);
    LPCOMM_PKG_HEADER  pPkgHeader = reinterpret_cast<LPCOMM_PKG_HEADER>(tptr + MSGHEADERLEN);//包头
    deleteFlag = false;             //是否释放发送缓冲区
    m_read_buffer = pPkgBody; //整个消息由消息头+包头+数据构成,取到数据部分
    m_read_idx = iBodyLength;
    HTTP_CODE ret = process_read();
    process_write(ret);
    pPkgHeader ->pkgLen =  strlen(m_write_buffer) + m_content_length + PKGHEADERLEN;    //填充包头内容
    pConn ->isendlen = pPkgHeader ->pkgLen;   //要发送的数据长度
    m_iov[0].iov_base = pPkgHeader;         //填充发送包头的长度和指针
    m_iov[0].iov_len = PKGHEADERLEN; 

    return true;
}