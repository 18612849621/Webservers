#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// 网络通信相关头文件
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/epoll.h>
#include <signal.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./httpconnection/http_conn.h"

#define MAX_FD 65535 // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 事件数组的最大容量

// handler是一个函数指针，右边(int)说明这个函数有一个int型参数，
// 左边的void说明这个的函数值返回值是void型
void addsig(int sig, void(handler)(int)) { // 添加信号捕捉
    struct sigaction sa; // 描述信号处理细节的结构体
    memset(&sa, '\0', sizeof(sa)); // 初始化
    sa.sa_handler = handler; // 绑定处理函数
    sigfillset(&sa.sa_mask); // 设置信号集掩码
    sigaction(sig, &sa, NULL); // 捕捉sig信号
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool oneshot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("按照如下格式运行: %s port_number\n", basename(argv[0])); // basename获取路径的最顶层文件名字
        exit(-1);
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 对SIGPIE信号进行处理 [默认会终止进程，这里用SIG_ING忽略]
    addsig(SIGPIPE, SIG_IGN);

    // 初始化线程池 [处理http连接请求]
    threadpool<http_conn> * pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    }catch(...) { // 如果抓到异常则直接退出
        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn * users = new http_conn[MAX_FD];
    int ret = 0; // 用于接收任务返回值
    int listenfd = socket(PF_INET, SOCK_STREAM, 0); // 创建监听socket

    // 设置端口复用
    int reuse = 1; // 为1代表复用
    ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    assert(ret != -1);
    
    // 创建ipv4 socket地址
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // socket命名
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    // 监听
    ret = listen(listenfd, 5); // logback = 5 说明最大可以监听请求数量为5左右
    assert(ret != -1);

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5); // 创建epoll内核事件表大小为5
    assert(epollfd != -1);

    // 将监听(listenfd)的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false); // 注意监听事件不可设置，这样会造成后续的客户请求无法收到
    // 注意监听listenfd如果是ET模式，需要对accpet进行循环使用，因为每一次epoll_wait()触发条件是需要监听对象有新变化才能就绪
    // 所以当同时有多个连接的时候，需要在通知有连接时，accpet建立所有连接，不然会造成TCP连接请求阻塞在监听socket的监听队列里
    // 导致一直无法建立连接，需要下一次来新的连接请求（监听socket状态改变）才可以。
    // 当然使用LT模式也无妨，不过会造成资源浪费
    // 将事件表初始化到http连接类
    http_conn::m_epollfd = epollfd;

    while (true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1); // num返回就绪事件的数量
        if ((num < 0) && (errno != EINTR)) { // 如果错误为EINTR说明读是由中断引起的
            printf("epoll failure\n");  // 调用epoll失败
            break;
        }

        // 循环遍历数组
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;  // 获取当前事件的文件描述符
            if (sockfd == listenfd) {// 有客户端连接
                // 初始化用户信息准备接收socket
                struct sockaddr_in client_address; // ipv4 socket地址
                socklen_t client_addrlen = sizeof(client_address);
                // 接收socket
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);

                if ( connfd < 0 ) { // 如果出现了错误
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if (http_conn::m_user_count >= MAX_FD) {
                    // 目前连接数满了
                    // 给客户端写一个信息：服务器正忙。（之后再写）
                    close(connfd);
                    continue;
                }
                
                // 将新的客户的数据初始化，放到数组中[connfd根据连接数量递增，所以直接用作数组的索引]
                users[connfd].init(connfd, client_address);
            }else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或者错误等事件
                users[sockfd].close_conn();
            }else if (events[i].events & EPOLLIN) { // 查看第i个事件是否就绪读
                // oneshot模式需要一次性全部读完
                if (users[sockfd].read()) {  // 开始读
                    pool->append(users + sockfd); // 加入到线程池任务
                }else { // 读取失败
                    users[sockfd].close_conn();
                }
            }else if (events[i].events & EPOLLOUT) { // 查看第i个事件是否就绪写
                if (!users[sockfd].write()) { // 没有成功一次性写完所有数据
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}