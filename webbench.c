#include "socket.c"
#include <unistd.h>
#include<stdio.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>
#include<string.h>
#include<error.h>


//用法和各参数的详细意义
static void usage(void)
{
    fprintf(stderr,
            "webbench [parameter]... URL\n"
            "  -f|--force               No waiting for server response \n"
            "  -r|--reload              Re-request loading (no caching) \n"
            "  -t|--time <sec>          Set run time in seconds, default 30 seconds \n"
            "  -p|--proxy <server:port> Setting the number of proxy servers \n"
            "  -c|--clients <n>         How many clients are created, default is 1 \n"
            "  -9|--http09              Using HTTP 0.9 protocol \n"
            "  -1|--http10              Using HTTP 1.0 protocol \n"
            "  -2|--http11              Using HTTP 1.1 protocol \n"
            "  -G|--get                 Using GET request method \n"
            "  -H|--head                Using HEAD request method \n"
            "  -O|--options             Using OPTIONS request method \n"
            "  -?|-h|--help             Display help information \n"
            "  -V|--version             Display program version information \n"  );
};

//支持的http请求方法
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3

//默认参数设置，一般需要自己传入命令行参数设置
int method=METHOD_GET; //默认请求方法为get
int clients=1;         //默认只模拟一个客户端
int force=0;           //默认需要等待服务器响应
int force_reload=0;    //失败时重新请求
int proxyport=80;      //默认访问服务器端口为80
char *proxyhost=NULL;  //默认无代理服务器
int benchtime=30;      //默认模拟请求时间为30s

//支持的http版本号
int http10=1;
/*
0表示http0.9
1表示http1.0
2表示http1.1
*/

/* 内部 */
int mypipe[2];                //管道用于父子进程通信
char host[MAXHOSTNAMELEN];    //存储服务器网络地址
#define REQUEST_SIZE 2048     //最大请求次数
char request[REQUEST_SIZE];   //存放http请求报文信息数组

//判断测试时长是否已经到达设定时间
volatile int timeout=0;
/*

 volatile:
 类型修饰符，作为指令关键字，
 确保本指令不会因为编译器优化而省略
 且每次要求重新读值，
 编译器在用到这个变量的时候都必须小心的重新读取这个变量的值，
 而不是使用保存在寄存器里的备份，保证每次读到的都是最新的

*/

//测试结果
int speed=0;  //成功得到服务器响应的子进程数量
int failed=0; //没有成功得到服务器响应的子进程数量
int bytes=0;  //所有子进程读取到服务器回复的总字节数

int connect_failed=0;
int send_failed=0;
int wclose_failed=0;
int read_failed=0;
int sclose_failed=0;



//程序版本号
#define PROGRAM_VERSION "1.5"

/* 函数声明 */

//子进程真正相服务器发出请求报文并以其得到此期间的相关数据
static void benchcore(const char* host,const int port, const char *req);

//父进程创建子进程，读取子进程测试得到的数据，然后统计处理
static int bench(void);

//构造http请求报文
static void build_request(const char *url);

//闹钟信号处理函数
static void alarm_handler(int signal)
{
    //到达设定的测压时间，则调用闹钟信号处理函数
    timeout=1;//timerexpired为1则会在循环中跳出测试
}

//构造长选项和短选项的对应
static const struct option long_options[]=
{
    {"force",no_argument,&force,1},
    {"reload",no_argument,&force_reload,1},
    {"time",required_argument,NULL,'t'},
    {"help",no_argument,NULL,'?'},
    {"http09",no_argument,NULL,'9'},
    {"http10",no_argument,NULL,'1'},
    {"http11",no_argument,NULL,'2'},
    {"get",no_argument,&method,METHOD_GET},
    {"head",no_argument,&method,METHOD_HEAD},
    {"options",no_argument,&method,METHOD_OPTIONS},
    {"version",no_argument,NULL,'V'},
    {"proxy",required_argument,NULL,'p'},
    {"clients",required_argument,NULL,'c'},
    {NULL,0,NULL,0}
};

int main(int argc, char *argv[])
{
    //argc表示参数个数
    //argv[0]表示自身运行的路径和程序名
    //argv[1]指向第1个参数
    //argv[n]指向第n个参数

    int opt=0;
    int options_index=0;
    char *tmp=NULL;

    //进行命令行参数的处理

    //1.命令行没有输入参数
    if(argc==1)
    {
        usage();//显示提示信息
        return 2;
    }

    //命令行有输入参数则一个个解析
    //"frt:p:c:?V912"中一个字符后面加一个冒号代表该命令后面接一个参数
    //比如t,p,c命令，后面都要接一个参数
    //连续两个冒号则表示参数可有可无
    while((opt=getopt_long(argc,argv,"frt:p:c:?V912GHO",long_options,&options_index))!=EOF )
    {
        switch(opt)
        {
        case 'f':
            force=1;//不等待服务器响应
            printf("No waiting for server response\n");
            break;

        case 'r'://重新请求加载(无缓存)
            force_reload=1;
            printf("Re-request loading (no caching)\n");
            break;

        case '9'://使用http/0.9协议来构造请求
            http10=0;
            printf("Using HTTP/0.9\n");
            break;

        case '1':
            http10=1;//使用http/1.0协议来构造请求
            printf("Using HTTP/1.0\n");
            break;

        case '2':
            http10=2;//使用http/1.1协议来构造请求
            printf("Using HTTP/1.1\n");
            break;

        case 'V':
            printf(PROGRAM_VERSION"\n");//显示程序版本信息
            exit(0);

        case 't'://设置运行时间，单位：秒，默认为30秒
            benchtime=atoi(optarg);//optarg指向选项后的参数
            printf("benchtime=%d\n",benchtime);
            break;

        case 'c'://创建多少个客户端，默认为1个
            clients=atoi(optarg);//同上
            printf("clients=%d\n",clients);
            break;

        case 'p'://使用代理服务器，则设置其代理网络号和端口号，格式：-p server:port

            //server:port是一个参数，下面把这个字符串解析成服务器地址和端口两个参数

            tmp=strrchr(optarg,':');//在optagr中找到':'最后出现的位置

            proxyhost=optarg;

            if(tmp==NULL)//没有端口号
            {
                break;
            }

            if(tmp==optarg)//端口号在optarg最开头，说明缺失主机地址
            {
                fprintf(stderr,"Option parameter error,Proxy server %s: Missing host name ",optarg);
                return 2;
            }
            if(tmp==optarg+strlen(optarg)-1)//':'在最末尾，说明缺失端口号
            {
                fprintf(stderr,"Option parameter error,Proxy server %s: Missing port number ",optarg);
                return 2;
            }

            *tmp='\0';//将optarg从':'开始截断，前面就是主机名，后面是端口号

            proxyport=atoi(tmp+1);//设置代理服务器端口号

            printf("Using proxy server %s:%d\n",proxyhost,proxyport);

            break;

        case 'G':
             method=METHOD_GET;
             printf("Using GET request method \n");
             break;
        case 'H':
             method=METHOD_HEAD;
             printf("Using HEAD request method \n");
             break;
        case 'O':
             method=METHOD_OPTIONS;
             printf("Using OPTIONS request method \n");
             break;
        case '?'://显示帮助信息
            usage();
            return 2;
            break;

        default://失败也显示帮助信息
            usage();
            return 2;
            break;
        }
    }

    //命令参数解析完毕之后，刚好是读到URL，此时argv[optind]指向URL
    //URL参数为空
    if(optind==argc)
    {
        fprintf(stderr,"Missing URL\n");
        usage();
        return 2;
    }

    //设置默认值
    if(clients==0)
        clients=1;
    if(benchtime==0)
        benchtime=30;

    //程序说明
    fprintf(stderr,"WebBench: A Lightweight Web Pressure Measuring Tool "PROGRAM_VERSION" covered by YB \nGPL Open Source Software\n");

    //构造请求报文
    build_request(argv[optind]);//参数为URL

    //请求报文构造好了，开始测压
    printf("\nIn testing :\n");

    //选择请求方法
    switch(method)
    {
    case METHOD_OPTIONS:
        printf("OPTIONS");
        break;

    case METHOD_HEAD:
        printf("HEAD");
        break;

    case METHOD_GET:
        printf("GET");
        break;
    default:
        printf("GET");
        break;

    }

    //打印URL
    printf(" %s",argv[optind]);

    switch(http10)
    {
    case 0:
        printf("(Using HTTP/0.9)");
        break;
    case 1:
        printf("(Using HTTP/1.0)");
        break;
    case 2:
        printf("(Using HTTP/1.1)");
        break;
    }

    printf("\n");

    printf("Operation parameters :\n");

    printf("%d Clients",clients);

    printf(",Testing running %d s",benchtime);

    if(force)
        printf(",Choose to close the connection ahead of time ");

    if(proxyhost!=NULL)
        printf(",Through proxy server %s:%d ",proxyhost,proxyport);

    if(force_reload)
        printf(",Choose no cache ");

    /*
     *换行不能少！库函数是默认行缓冲，子进程会复制整个缓冲区
     *若不换行刷新缓冲区,子进程会把缓冲区的也打出来
     *而换行后缓冲区就刷新了
     *子进程的标准库函数的那块缓冲区就不会有前面这些了
    */
    printf(".\n");

    //真正开始压力测试！
    return bench();
}

//父进程创建子进程，读子进程测试到的数据，然后统计处理
static int bench(void)
{
    int i,j;
    int k;
    int c1,c2,c3,c4,c5;

    pid_t pid=0;//进程号定义 实际上也是int型的
    FILE *f;//文件

    //先检查一下目标服务器是可用性
    i=Socket(proxyhost==NULL?host:proxyhost,proxyport);

    //目标服务器不可用
    if(i<0)
    {
        fprintf(stderr,"\n Connection server failed, interrupt test \n");
        return 3;
    }

    //尝试连接成功了，关闭连接
    close(i);

    //建立父子进程通信的管道
    if(pipe(mypipe))
    {
        perror(" Communication Pipeline Failure ");
        return 3;
    }


    /*
    父进程创建子进程后，fork函数是让子进程完全拷贝父进程，
    包括父进程上下文，什么意思呢？
    就是说父进程的EIP(CPU的下一条指令地址)以及变量等等一律拷贝，
    也就是说，父进程执行过的代码子进程是不会再执行，
    子进程下一条该执行的命令与父进程完全一样！！！
    */
    //创建子进程进行测试，子进程数量和clients有关
    for(i=0; i<clients; i++)
    {
        // pid 为 pid_t 类型 表示进程号

        pid=fork();//建立子进程

        //fork失败 子进程错误
        if(pid <= (pid_t) 0)
        {
            sleep(1);  //当前进程挂起1毫秒，将cpu时间交给其他进程
            break;     //跳出去，阻止子进程继续fork
        }
    }

    //处理fork失败情况
    if( pid < (pid_t) 0)
    {
        fprintf(stderr,"The %d Subprocess creation failed ",i);
        perror(" Failure to create subprocesses ");
        return 3;
    }

    //当前进程是子进程
    if(pid == (pid_t) 0)
    {

        //由子进程发出请求报文 根据是否采用代理发送不同的报文
        if(proxyhost==NULL)
            benchcore(host,proxyport,request);
        else
            benchcore(proxyhost,proxyport,request);

        //子进程获得管道写端的文件指针，准备向父进程写结果
        f=fdopen(mypipe[1],"w");

        //管道写端打开失败
        if(f==NULL)
        {
            perror(" Pipeline Writer End Failed to Open ");
            return 3;
        }


        /*向管道中写入该孩子进程在一定时间内
          请求成功的次数
          失败次数
          读取到服务器回复的总字节数
        */
        fprintf(f,"%d %d %d %d %d %d %d %d\n",speed,failed,bytes,connect_failed,send_failed,wclose_failed,read_failed,sclose_failed);

        //关闭写端
        fclose(f);

        return 0;
    }
    //当前进程是父进程
    else
    {
        //父进程获得管道读端的文件指针
        f=fdopen(mypipe[0],"r");

        //管道读端打开失败
        if(f==NULL)
        {
            perror(" Pipeline Reader Failed to Open ");
            return 3;
        }

        /*
        fopen标准IO函数是自带缓冲区的
        我们输入的数据非常短，并且数据要及时
        所以没有缓冲是最合适的
        我们不需要缓冲区
        因此把缓冲类型设置为_IONBF*/
        setvbuf(f,NULL,_IONBF,0);

        speed=0;  //连接成功次数，后面除以时间可以得到速度
        failed=0; //失败的请求次数
        bytes=0;  //服务器回复的总字节数

        connect_failed=0;
        send_failed=0;
        wclose_failed=0;
        read_failed=0;
        sclose_failed=0;


        //父进程不停的读
        while(1)
        {
            //读入参数以及得到成功得到的参数的个数
            pid=fscanf(f,"%d %d %d %d %d %d %d %d",&i,&j,&k,&c1,&c2,&c3,&c4,&c5);

            //成功得到的参数个数小于8
            if(pid<8)
            {
                fprintf(stderr,"A child process deaid\n");
                break;
            }

            //计总数
            speed+=i;
            failed+=j;
            bytes+=k;

            connect_failed+=c1;
            send_failed+=c2;
            wclose_failed+=c3;
            read_failed+=c4;
            sclose_failed+=c5;



            if(--clients==0)//记录已经读了多少个子进程的数据，读完就退出
                break;
        }

        //关闭读端
        fclose(f);

        //统计处理结果
        printf("\nSpeed:%d pages/min,%lld bytes/s.\nRequest:%d Success,%d Fail\n",\
              (int)((speed+failed)/(benchtime/60.0f)),\
              (int)(bytes/(float)benchtime),\
              speed,failed);

        //失败的类型及个数
        printf("Reasons for failure:\n");
        printf("connect failed:%d\n",connect_failed);
        printf("send message failed:%d\n",send_failed);
        printf("write-side shutdown failed:%d\n",wclose_failed);
        printf("read server message failed:%d\n",read_failed);
        printf("socket close failed:%d\n",sclose_failed);

    }

    return i;
}

//子进程真正向服务器发送请求报文并以其得到期间相关数据
void benchcore(const char *host,const int port,const char *req)
{
    int rlen;
    char buf[1500];//记录服务器响应请求返回的数据
    int s,i;
    struct sigaction sa;//信号处理函数定义

    //设置alarm_handler函数为闹钟信号处理函数
    sa.sa_handler=alarm_handler;
    sa.sa_flags=0;

    if(sigaction(SIGALRM,&sa,NULL))//超时会产生信号SIGALRM，用sa中指定函数处理
        exit(3);

    alarm(benchtime);//开始计时

    rlen=strlen(req);//得到请求报文的长度

nexttry:
    while(1)
    {
        //只有在收到闹钟信号后会使得timeout=1
        if(timeout)//超时返回
        {
            //修正失败信号
            if(failed>0)
                failed--;
            if(connect_failed>0)
                connect_failed--;
            else if(send_failed>0)
                send_failed--;
            else if(wclose_failed>0)
                wclose_failed--;
            else if(read_failed>0)
                read_failed--;
            else if(sclose_failed>0)
                sclose_failed--;

            return;
        }

        //建立到目的网站的tcp连接,发送http请求
        s=Socket(host,port);

        //连接失败
        if(s<0)
        {
            failed++;//失败次数+1
            connect_failed++;
            continue;
        }

        //发出请求报文
        if(rlen!=write(s,req,rlen))//write函数会返回实际写入的字节数
        {
            failed++;//实际写入的字节数和请求报文字节数不相同，写失败，发送1失败次数+1
            send_failed++;
            close(s);//写失败了也不要忘记关闭套接字
            continue;
        }

        //http/0.9的特殊处理
        /*
         *因为http/0.9是在服务器回复后自动断开连接
         *在此可以提前先彻底关闭套接字的写的一半，如果失败了那肯定是个不正常的状态
         *事实上，关闭写后，服务器没有写完数据也不会再写了，这个就不考虑了
         *如果关闭成功则继续往后，因为可能还需要接收服务器回复的内容
         *当这个写一定是可以关闭的，因为客户端也不需要写，只需要读
         *因此，我们主动破坏套接字的写，但这不是关闭套接字，关闭还是得用close
        */
        if(http10==0)
        {
            if(shutdown(s,1))//1表示关闭写 关闭成功返回0，出错返回-1
            {
                failed++;//关闭出错，失败次数+1
                wclose_failed++;
                close(s);//关闭套接字
                continue;
            }
        }

        //foece=0 默认需要等待服务器回复
        if(force==0)
        {
            //从套接字读取所有服务器回复的数据
            while(1)
            {
                //超时标志为1，不再读取服务器回复的数据
                if(timeout)
                    break;

                //读取套接字中1500个字节数据到buf数组中
                i=read(s,buf,1500);//如果套接字中数据小于要读取的字节数1500会引起阻塞 返回-1

                //read返回值：

                //未读取任何数据   返回   0
                //读取成功         返回   已经读取的字节数
                //阻塞             返回   -1


                //读取阻塞了
                if(i<0)
                {
                    failed++;       //失败次数+1
                    read_failed++;
                    close(s);       //关闭套接字，不然失败次数多会严重浪费资源
                    goto nexttry;   //这次失败了那么继续请求下一次连接和发出请求
                }
                //读取成功
                else
                {
                    if(i==0)
                        break;//没有读取到任何字节数
                    else
                        bytes+=i;//从服务器读取到的总字节数增加
                }
            }
        }

        /*

        close返回返回值
        成功   返回 0
        失败   返回 -1

        */

        //套接字关闭失败
        if(close(s))
        {
            failed++;//没有成功得到服务器响应的子进程数量
            sclose_failed++;
            continue;
        }

        //套接字关闭成功 成功得到服务器响应的子进程数量+1
        speed++;
    }
}

//构造http报文请求到request数组
/*

典型的http/1.1的get请求如下：

从下一行开始
GET /test.jpg HTTP/1.1  //请求行：请求方法+url+协议版本
User-Agent: WebBench 1.5
Host:192.168.10.1
Pragma: no-cache
Connection: close

//从上行结束，最后必须要有一个空行

该函数目的就是根据需求填充出这样一个http请求放到request报文请求数组中
*/
void build_request(const char *url)
{
    //存放端口号的中间数组
    char tmp[10];
    //存放url中主机名开始的位置
    int i;

    //初始化
    memset(host,0,MAXHOSTNAMELEN);
    memset(request,0,REQUEST_SIZE);


    //判断应该使用的http协议

    //1.缓存和代理都是都是http/1.0以后才有到的
    if(force_reload && proxyhost!=NULL && http10<1)
        http10=1;

    //2.head请求是http/1.0后才有的
    if(method==METHOD_HEAD && http10<1)
        http10=1;

    //3.options请求和reace请求都是http/1.1才有
    if(method==METHOD_OPTIONS && http10<2)
        http10=2;
    if(method==METHOD_TRACE && http10<2)
        http10=2;

    //开始填写http请求


    //填充请求方法到请求行
    switch(method)
    {
    default:
    case METHOD_GET:
        strcpy(request,"GET");
        break;
    case METHOD_HEAD:
        strcpy(request,"HEAD");
        break;
    case METHOD_OPTIONS:
        strcpy(request,"OPTIONS");
        break;
    case METHOD_TRACE:
        strcpy(request,"TRACE");
        break;
    }

    //按照请求报文格式在请求方法后填充一个空格
    strcat(request," ");

    //判断url的合法性

    //1.url中没有 "://" 字符
    if(NULL==strstr(url,"://"))
    {
        fprintf(stderr,"\n %s:is an illegal URL\n",url);
        exit(2);//结束当前进程 2表示是因为url不合法导致进程停止的
    }
    //2.url过长
    if(strlen(url)>1500)
    {
        fprintf(stderr,"URL too long\n");
        exit(2);
    }

    //3.若无代理服务器，则只支持http协议
    if(proxyhost==NULL)
    {
        //忽略字母大小写比较前7位
        if (0!=strncasecmp("http://",url,7))
        {
            fprintf(stderr,"\n URL can't be parsed, need it or not, but don't choose to use proxy server\n");
            usage();
            exit(2);
        }
    }

    //在url中找到主机名开始的地方
    //比如：http://baidu.com:80/
    //主机名开始的地方为bai....
    //i==7
    i=strstr(url,"://")-url+3;

    //4.从主机名开始的地方开始往后找，没有 '/' 则url非法
    if(strchr(url+i,'/')==NULL)
    {
        fprintf(stderr,"\n URL illegal: hostname does not end with'/' \n");
        exit(2);
    }
    //url合法性判断到此结束

    //开始填写url到请求行

    //无代理时
    if(proxyhost==NULL)
    {
        //存在端口号 比如http://www.baidu.com:80/
        if(index(url+i,':')!=NULL && index(url+i,':')<index(url+i,'/'))
        {
            //填充主机名到host字符数组，比如www.baidu.com
            strncpy(host,url+i,strchr(url+i,':')-url-i);

            //初始化存放端口号的中间数组
            memset(tmp,0,10);

            //切割得到端口号
            strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
            /* printf("tmp=%s\n",tmp); */

            //设置端口号 atoi将字符串转整型
            proxyport=atoi(tmp);

            //避免写了';'却没有写端口号，这种情况下默认设置端口号为80
            if(proxyport==0)
                proxyport=80;
        }
        //不存在端口号
        else
        {
            //填充主机名到host字符数组，比如www.baidu.com
            strncpy(host,url+i,strcspn(url+i,"/"));
        }
        // printf("Host=%s\n",host);

        //将主机名，以及可能存在的端口号以及请求路径填充到请求报文中
        //比如url为http://www.baidu.com:80/one.jpg/
        //就是将www.baidu.com:80/one.jpg填充到请求报文中
        strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
    }
    //存在代理服务器时就比较简单了，直接填写，不用自己处理
    else
    {
        // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);

        //直接将url填充到请求报文
        strcat(request,url);
    }

    //填充http协议版本到请求报文的请求行
    if(http10==1)
        strcat(request," HTTP/1.0");
    else if (http10==2)
        strcat(request," HTTP/1.1");

    //请求行填充结束，换行
    strcat(request,"\r\n");


    //填写请求报文的报头
    if(http10>0)
        strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");

    //不存在代理服务器且http协议版本为1.0或1.1，填充Host字段
    //当存在代理服务器或者http协议版本为0.9时，不需要填充Host字段
    //因为http0.9版本没有Host字段，而代理服务器不需要Host字段
    if(proxyhost==NULL && http10>0)
    {
        strcat(request,"Host: ");
        strcat(request,host);//Host字段填充的是主机名或者IP
        strcat(request,"\r\n");
    }

    /*pragma是http/1.1之前版本的历史遗留问题，仅作为与http的向后兼容而定义
    规范定义的唯一形式：
    Pragma:no-cache
    若选择强制重新加载，则选择无缓存
    */
    if(force_reload && proxyhost!=NULL)
    {
        strcat(request,"Pragma: no-cache\r\n");
    }

    /*我们的目的是构造请求给网站，不需要传输任何内容，所以不必用长连接
    http/1.1默认Keep-alive(长连接）
    所以需要当http版本为http/1.1时要手动设置为 Connection: close
    */
    if(http10>1)
        strcat(request,"Connection: close\r\n");

    //在末尾填入空行
    if(http10>0)
        strcat(request,"\r\n");

    //fprintf("\nRequest:\n%s\n",request);
}
