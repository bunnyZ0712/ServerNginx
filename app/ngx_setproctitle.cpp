/*设置argv参数与环境变量*/
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "ngx_global.h"

//环境变量与传入参数在一片连续空间中
void ngx_init_setproctitle(void)    //将环境变量存放到新位置，防止被覆盖
{
    int envirlength = 0;
    for(int i = 0; environ[i]; i++)
    {
        envirlength += strlen(environ[i]) + 1; //字符后面会有结束符\0，所以要加一
    }
    g_environlen = envirlength;
    gp_envmem = new char[envirlength];  //获取新空间用来存放环境变量
    if(gp_envmem == nullptr)
    {
        throw std::logic_error("gp_envmem new fail\n");
    }
    bzero(gp_envmem, envirlength);
    char* tptr = gp_envmem;

    for(int i = 0; environ[i]; i++)         //将环境变量存放到新位置，防止被覆盖
    {
        int length = strlen(environ[i]) + 1;
        memcpy(tptr, environ[i], length);
        environ[i] = tptr;
        tptr += length;
    }
}

void ngx_setproctitle(const char *title)
{
    size_t titlelength = strlen(title) + 1;

    size_t argvlength = 0;
    for(int i = 0; g_os_argv[i]; i++)
    {
        argvlength += strlen(g_os_argv[i]) + 1;
    }
    size_t esy = argvlength + g_environlen;     //计算argv与环境变量总体空间大小，新写入的数据不能大于这个空间

    if(esy < titlelength)
    {
        return;
    }
    
    g_os_argv[1] = nullptr; //一般以nullptr作为结尾

    char* tptr = g_os_argv[0];                      //将新的参数放置到argv[0]，其他的剩余空间全置0
    memcpy(g_os_argv[0], title, titlelength);
    tptr += titlelength;

    bzero(tptr, esy - titlelength);
}