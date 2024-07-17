#include "ngx_c_sslctx.h"

#include <openssl/err.h>

std::shared_ptr<SSLCTXServer> SSLCTXServer::mInstance =  nullptr;

SSLCTXServer::SSLCTXServer(const char* crtFile, const char* keyFile, const char* caFile)
{
    int ret;
    mSslCtx = SSL_CTX_new(TLS_server_method());
    if(mSslCtx ==  nullptr)
    {
        ERR_print_errors_fp(stderr);
        return;
    }
    
    //载入用户的数字证书， 此证书用来发送给客户端。证书里包含有公钥
    ret = SSL_CTX_use_certificate_file(mSslCtx, crtFile, SSL_FILETYPE_PEM); 
    if(ret <= 0)
    {
        SSL_CTX_free(mSslCtx);
        ERR_print_errors_fp(stderr);
        return;
    }
    //载入用户私钥
    ret = SSL_CTX_use_PrivateKey_file(mSslCtx, keyFile, SSL_FILETYPE_PEM);
    if(ret <= 0)
    {
        SSL_CTX_free(mSslCtx);
        ERR_print_errors_fp(stderr);
        return;
    }
    //检查用户私钥是否正确
    ret = SSL_CTX_check_private_key(mSslCtx);
    if(ret <= 0)
    {
        SSL_CTX_free(mSslCtx);
        ERR_print_errors_fp(stderr);
        return;
    }
}

SSLCTXServer::~SSLCTXServer()
{
    SSL_CTX_free(mSslCtx);
}

void SSLCTXServer::DeleteSSLCTX(SSLCTXServer* ptr)
{
    delete ptr;
}

void SSLCTXServer::Create(const char* crtFile, const char* keyFile, const char* caFile)
{   
    if(mInstance == nullptr)
    {
        mInstance.reset(new SSLCTXServer(crtFile, keyFile, caFile), DeleteSSLCTX);
    }
}

std::shared_ptr<SSLCTXServer> SSLCTXServer::GetInstance(const char* crtFile, const char* keyFile, const char* caFile)
{
    static std::once_flag createFlag;
    std::call_once(createFlag, Create, crtFile, keyFile, caFile);
    
    return mInstance ->shared_from_this();
}

std::shared_ptr<SSLCTXServer> SSLCTXServer::GetInstance(void)
{
    return mInstance ->shared_from_this();
}

MYSSL* SSLCTXServer::NewSSL(int sockFd)
{
    //申请SSL连接
    auto sslptr = SSL_new(mSslCtx);
    if(sslptr == nullptr)
    {
        ERR_print_errors_fp(stderr);
        return nullptr;       
    }
    auto myssl = new MYSSL(sslptr, sockFd);

    return myssl;
}