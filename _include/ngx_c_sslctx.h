#ifndef NGX_C_SSLCTX_H
#define NGX_C_SSLCTX_H

#include <openssl/ssl.h>
#include <sys/uio.h>
#include <memory>
#include <mutex>
#include <iostream>

#include "ngx_c_ssl.h"

class SSLCTXServer : public std::enable_shared_from_this<SSLCTXServer>{
private:
    SSLCTXServer(const char* crtFile, const char* keyFile, const char* caFile);
    ~SSLCTXServer();

private:
    static std::shared_ptr<SSLCTXServer> mInstance;
    static void Create(const char* crtFile, const char* keyFile, const char* caFile);
    static void DeleteSSLCTX(SSLCTXServer* ptr);
    
public:
    static std::shared_ptr<SSLCTXServer> GetInstance(const char* crtFile, const char* keyFile, const char* caFile = nullptr);
    static std::shared_ptr<SSLCTXServer> GetInstance(void);
    MYSSL* NewSSL(int sockFd);

private:
    SSL_CTX* mSslCtx;
};


#endif