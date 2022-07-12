#pragma once
// 将所有定时器中超时时间最小的一个定时器的超时值作为心搏间隔。
// 这样，一旦心搏函数tick被调用，超时时间最小的定时器必然到期，
// 我们就可以在tick函数中处理该定时器。
// 因为我的服务器比较小，所以才用的时间堆方式，如果更大的话会使用多时间轮盘的方式
#include <iostream>
#include <netinet/in.h>
#include <time.h>

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>


using std::exception;

#define BUFFER_SIZE 64

class heap_timer; /*先声明一下类名*/

struct client_data { /*绑定socket和定时器*/
    sockaddr_in address;
    int sockfd; // socket文件描述符
    char buf[BUFFER_SIZE];
    heap_timer* timer; // 定时器
};

class heap_timer { // 定时器类 封装好用户信息和时间戳
    public:
        heap_timer() {  // 等待初始化
            
        }

        heap_timer(int delay) {  // time（NULL）可以获得从1970年1月1日至今所经历的时间（以秒为单位）
            expire = time(NULL) + delay;
        }
    public:
        time_t expire; // 定时器生效的绝对时间
        void (*cb_func)(client_data *); // 定时器的回调函数
        client_data* user_data; // 用户的数据
};

//时间堆类
class timer_heap{
    public:
        // 构造函数1，初始化一个大小为cap的空堆
        timer_heap(int cap) throw (std::exception); // 创建堆数组
        // 构造函数2， 用已有数组来初始化堆
        timer_heap(heap_timer ** init_array, int size, int capacity) throw (std::exception); // 用已经有的来创建
        ~timer_heap(); // 销毁时间堆
    private:
        heap_timer ** array; // 堆数组[存储每一个时间堆对象]
        int capacity;        // 堆数组的容量
        int cur_size;        // 堆数组当前包含的元素个数
    public: // 对外借口
        void add_timer(heap_timer * timer) throw (std::exception); //添加目标定时器timer
        void del_timer(heap_timer * timer); // 删除目标定时器
        void adjust_timer(heap_timer * timer); // 调整新的定时器
        heap_timer * top() const; // 获得堆顶部的定时器
        void pop_timer(); // 删除堆顶部的定时器
        void tick(); // 心搏函数
        bool empty() const { return cur_size == 0;} // 判断当前堆是否为空
    private: // 函数自身功能接口
        // 最小堆的下滤操作，确保堆数组中以第hole个节点作为根的子树拥有最小堆性质
        void percolate_down(int hole);
        void resize() throw (std::exception); // 将堆数组容量扩大1倍
};

// 最顶层封装成工具类
class Utils{
    public:
        Utils() {}
        ~Utils() {}

        void init(int timeslot);

        //对文件描述符设置非阻塞
        int setnonblocking(int fd);

        //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
        void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

        //信号处理函数
        static void sig_handler(int sig);

        //设置信号函数
        void addsig(int sig, void(handler)(int), bool restart = true);

        //定时处理任务，重新定时以不断触发SIGALRM信号
        void timer_handler();

        void show_error(int connfd, const char *info);

    public:
        static int *u_pipefd;
        timer_heap m_timer_lst = timer_heap(65536); // 需要在这里初始化一下
        static int u_epollfd;
        int m_TIMESLOT; // 定义时间间隔
};

void cb_func(client_data *user_data);