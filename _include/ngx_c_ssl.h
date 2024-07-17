#ifndef NGX_C_SSL_H
#define NGX_C_SSL_H

#include <sys/uio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

class MYSSL{
public:
    MYSSL(SSL* tSSL, int sockFd);
    ~MYSSL();

public:
    bool Connect(void);
    bool Accept(void);
    int Read(void* readBuf, size_t len);
    int Write(const void* writeBuf, size_t length);  //为了配合send函数  

private:
    SSL* mSSL;
};

#endif