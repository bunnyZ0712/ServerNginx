#include "ngx_c_ssl.h"
#include <memory>
#include <iostream>

MYSSL::MYSSL(SSL* tSSL, int sockFd)
{
    this ->mSSL = tSSL;
    SSL_set_fd(mSSL, sockFd);
}

MYSSL::~MYSSL()
{
    while(!SSL_shutdown(mSSL)){}
    SSL_free(mSSL);    
}

bool MYSSL::Connect(void)
{
    int ret = SSL_connect(mSSL);
    if(ret <= 0)
    {
        ERR_print_errors_fp(stderr);
        return false;
    }    
    return true;
}

bool MYSSL::Accept(void)
{
    int ret = SSL_accept(mSSL);
    if(ret <= 0)
    {
        return false;
    }
    return true;
}

int MYSSL::Read(void* readBuf, size_t len)
{
    return SSL_read(mSSL, readBuf, len);
}

int MYSSL::Write(const void* writeBuf, size_t length)
{
    return SSL_write(mSSL, writeBuf, length);
}
