#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

#include "ngx_c_socket.h"


//将socket绑定的地址转换为文本格式【根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度】
//参数sa：客户端的ip地址信息一般在这里。
//参数port：为1，则表示要把端口信息也放到组合成的字符串里，为0，则不包含端口信息
//参数text：文本写到这里
//参数len：文本的宽度在这里记录
size_t CSocket::ngx_sock_ntop(struct sockaddr *sa, int port, u_char *text, size_t len) //根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度
{
    char ip[INET_ADDRSTRLEN];
    sockaddr_in* addr = (sockaddr_in*)sa;
    int Port = ntohs(addr->sin_port);
    std::string Ptext;

    switch(addr->sin_family)
    {
        case AF_INET : 
            inet_ntop(AF_INET, &addr->sin_addr.s_addr, ip, sizeof(sockaddr_in));
            Ptext = ip;
            Ptext = Ptext + " : " + std::to_string(Port);
            memcpy(text, Ptext.c_str(), Ptext.size());   
            return  Ptext.size();      
            break;
        default :
            return 0;
            break; 
    }
    return 0;
}