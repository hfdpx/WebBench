#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/*

sockaddr_in分析：

#include <netinet/in.h>和#include <arpa/inet.h>定义的

struct sockaddr
{
    __SOCKADDR_COMMON (sa_);  //协议族

    char sa_data[14];         //地址+端口号
};

sockaddr缺陷：把目标地址和端口号混在一起了
而sockaddr_in就解决了这一缺陷
将端口号和IP地址分开存储

struct sockaddr_in
{
    sa_family_t sin_family;     //地址族

    uint16_t sin_port;          //16位TCP/UDP端口号

    struct in_addr sin_addr;    //32位IP地址

    char sin_zero[8];           //不使用，只为了内存对齐
};

*/

/*

hostent分析：
host entry的缩写
记录主机信息包括主机名，别名，地址类型，地址长度和地址列表

struct hostent
{

    char *h_name;         //正式主机名

    char **h_aliases;     //主机别名

    int h_addrtype;       //主机IP地址类型：IPV4-AF_INET

    int h_length;		  //主机IP地址字节长度，对于IPv4是四字节，即32位

    char **h_addr_list;	  //主机的IP地址列表

};
#define h_addr h_addr_list[0]   //保存的是IP地址

主机的的地址是列表形式的原因：
当一个主机又多个网络接口时，自然有多个地址

*/

//host        ip地址或者主机名
//clientPort  端口
int Socket(const char *host, int clientPort)
{
    int sock;
    unsigned long inaddr;

    struct sockaddr_in ad;//地址信息
    struct hostent *hp;//主机信息

    /*

    因为host可能是ip地址或者主机名
    所以当host为主机名的时候需要通过主机名得到IP地址

    */
    //初始化地址
    memset(&ad, 0, sizeof(ad));

    //采用TCP/IP协议族
    ad.sin_family = AF_INET;

    //点分十进制IP转化为二进制IP
    inaddr = inet_addr(host);

    //输入为IP地址
    if (inaddr != INADDR_NONE)
        //将IP地址复制给ad的sin_addr属性
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    //输入不是IP地址，是主机名
    else
    {
        //通过主机名得到主机信息
        hp = gethostbyname(host);

        //没有得到主机信息
        if (hp == NULL)
            return -1;
        //将IP地址复制给ad的sin_addr属性
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }

    /*
    将端口号从主机字节顺序变成网络字节顺序
    就是整数在地址空间存储方式变为高字节存放在内存低字节处

    网络字节顺序是TCP/IP中规定好的一种数据表示格式，与CPU和操作系统无关
    从而可以保证数据在不同主机之间传输时能够被正确解释
    网络字节顺序采用大尾顺序：高字节存储在内存低字节处
    */
    ad.sin_port = htons(clientPort);

    /*
    AF_INET:     IPV4网络协议
    SOCK_STRAM:  提供面向连接的稳定数据传输，即TCP协议
    */
    //创建一个采用IPV4和TCP的socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    //创建socket失败
    if (sock < 0)
        return sock;

    //建立连接 连接失败返回-1
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;

    //创建成功 返回socket
    return sock;
}

