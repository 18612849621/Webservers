# pragma once
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
#include "./log/log.h"
#include "./corm/corm.h"
#include "./timer/timer.h"

#define MAX_FD 65535 // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 事件数组的最大容量
const int TIMESLOT = 5;             //最小超时单位

// 设置文件描述符非阻塞
extern int setnonblocking(int fd);
// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool oneshot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

class WebServers {
    public:
        WebServers(int _port, string userName, string passWord); // 默认是开启异步日志
        ~WebServers(); // 析构函数
        void servers_start(); // 启动循环
    private:
        /*————————————————————————————————初始化各模块——————————————————————————————————*/
        void init_log(); // 日志
        void init_threadpool(); // 线程池
        void init_tcp(); // 初始化监听和连接
        void init_epoll(); // 初始化epoll
        void init_orm(); // 初始化orm
        void init_pipe(); // 初始化管道
        /*—————————————————————————————————工具模块—————————————————————————————————————*/
        void addsig(int sig, void(handler)(int)); // 添加信号捕捉
        void timer(int connfd, struct sockaddr_in client_address);
        // heap_timer 定时器类
        void adjust_timer(heap_timer * timer);
        void deal_timer(heap_timer * timer, int sockfd);
        bool dealwithsignal(bool & timeout, bool &stop_server);
    private:
        /*——————————————————————————————————初始化日志——————————————————————————————————————*/
        // mysql相关内容
        string m_userName;
        string m_passWord;
        string m_databaseName;
        int m_port;
        int m_sql_num;
        string m_sqlurl; // mysql的ip
        sql_connection_pool * m_sql_connPool; // mysql连接池
        
        int m_close_log;  // 用于控制是否开关日志系统 0 : 启用
        int m_asny; // 如果是0 则代表同步日志，如果大于0 则说明是异步日志中队列的长度
        int port; // 端口号
        int m_pipefd[2]; // 管道 用于通信
        int ret; // 用于接收任务返回值
        int listenfd; // 声明监听套接字
        http_conn * users; // 创建一个数组指针 指向所有的客户端信息
        // 创建ipv4 socket地址
        struct sockaddr_in address;
        // 创建epoll对象，事件数组，添加
        epoll_event events[MAX_EVENT_NUMBER];
        int epollfd; // 声明epoll内核事件表
        threadpool<http_conn> * pool; // 工作线程池

        //定时器相关
        client_data *users_timer; // 定时器与socket绑定的数组
        Utils utils;
};