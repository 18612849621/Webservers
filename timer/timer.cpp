#include "timer.h"
#include "../httpconnection/http_conn.h"

// 构造&析构函数
timer_heap::timer_heap(int cap) throw (std::exception) : capacity(cap), cur_size(0) {
    array = new heap_timer * [capacity]; // 创建堆数组
    if (!array) { // 如果没有成功创建
        throw std::exception();
    }
    for (int i = 0; i < capacity; ++i) { // 初始化一下数组
        array[i] = NULL;
    }
}

timer_heap::timer_heap(heap_timer ** init_array, int size, int cap) throw (std::exception) : capacity(cap), cur_size(size) {
    if (capacity < size) { // 如果设置的容量不足承载下当前size
        throw std::exception();
    }
    array = new heap_timer * [capacity]; // 创建堆数组
    if (!array) { // 如果没有成功创建则抛出异常
        throw std::exception(); 
    }
    // 初始化堆数组
    for (int i = 0; i < capacity; ++i) {
        array[i] = NULL;
    }
    if (size != 0) { // 就是有东西
        for (int i = 0; i < size; ++i) {
            array[i] = init_array[i];
        }
        for (int i = (cur_size - 1) / 2; i >= 0; --i) { // 对于每一元素进行堆排序
            percolate_down(i);
        }
    }
}

timer_heap::~timer_heap() { // 析构函数
    for (int i = 0; i < capacity; ++i) {
        delete array[i]; // 释放每一个里面的对象
    }
    delete [] array; //释放指针数组
}

// 外部调用接口
// 调整定时器
void timer_heap::adjust_timer(heap_timer * timer) {
    del_timer(timer);
    add_timer(timer);
}

// 添加定时器
void timer_heap::add_timer(heap_timer * timer) throw (std::exception) {
    if (!time) { // 说明不合规矩
        return;
    }
    // 如果当前堆数组容量不够，则将其扩大1倍
    if (cur_size >= capacity) {
        resize(); // 扩容
    }
    // 新插入一个元素， 当前堆大小加1，hole是新建空穴的位置
    int hole = cur_size++;
    int parent = 0;
    // 对从空穴到根节点的路径上的所有节点执行上率操作
    while (hole > 0) {
        parent = (hole - 1) / 2; // 找父亲节点
        if (array[parent]->expire <= timer->expire) {
            break;
        }
        array[hole] = array[parent];
        hole = parent;
    }
    array[hole] = timer;
}

// 删除定时器
void timer_heap::del_timer(heap_timer * timer) {
    if (!timer) {
        return;
    }
    // 仅仅将目标定时器的回调函数设置为空，即所谓的延迟销毁这将节省真正删除该定时器产生的开销，但这样做容易让数组膨胀
    timer->cb_func = NULL; // 这样多了以后会产生冗余
}

// 获得堆顶部的定时器
heap_timer * timer_heap::top() const {
    if (empty()) {
        return NULL;
    }
    return array[0];
}
// 删除堆顶部的定时器
void timer_heap::pop_timer() {
    if (empty()) {
        return;
    }
    if (array[0]) {
        delete array[0]; // 删除堆顶那个元素
        // 将原来的堆顶元素替换为堆数组中最后一个元素
        array[0] = array[cur_size--];
        // 对于新的顶进行下滤操作，使得其放在正确的位置
        percolate_down(0);
    }
}
// 心搏函数
void timer_heap::tick() { // 处理所有的过期内容
    heap_timer * tmp = array[0]; // 获得堆顶的元素
    time_t cur = time(NULL); // 获取当前的时间 循环处理堆中到期的定时器 只要是满足这个时间戳的都去掉
    while (!empty()) {
        if (!tmp) { // 有问题就退出
            break;
        }
        // 如果堆顶定时器没有到期，退出循环
        if (tmp->expire > cur) {
            break;
        }
        // 否则执行堆顶定时器中的任务
        if (array[0]->cb_func) { // 这个就是函数指针
            array[0]->cb_func(array[0]->user_data); // 将每一个对象的数据传入到啊回调函数中
        }
        pop_timer(); // 剔除堆顶
        tmp = array[0]; // 更新出一个新的顶
    }
}

// 内部函数
void timer_heap::percolate_down(int hole) { // 针对array中第hold个节点进行处理
    heap_timer * temp = array[hole]; // 获取第hole个定时器对象[标杆]
    int child = 0;
    while ( (hole * 2 + 1) < cur_size ) {
        child = hole * 2 + 1; // hole * 2 + 1 就是找左孩子节点
        // 由于是最小堆，所以在排序的时候需要选择更小的孩子节点
        if (child < cur_size && array[child]->expire > array[child + 1]->expire) {
            child++; // 选择更小的子节点进行交换
        }
        if (array[child]->expire > temp->expire) { // 说明找到位置了【当前的比孩子小】
            break;
        }
        array[hole] = array[child];
        hole = child;
    }
    array[hole] = temp; // 收尾
}

void timer_heap::resize() throw (std::exception) { // 将堆数组容量扩大1倍
    heap_timer ** temp = new heap_timer * [2 * capacity]; // 扩大一倍
    for (int i = 0; i < 2 * capacity; ++i) { // init
        temp[i] = NULL;
    }
    if (!temp) {
        throw std::exception();
    }
    capacity = 2 * capacity;
    for (int i = 0; i < cur_size; ++i) { // 把之前的复制进去
        temp[i] = array[i];
    }
    delete [] array; // 清除指针对应的内容
    array = temp; 
}

// Util类：统一事件源
void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0); /*将信号值写入管道，以通知主循环，将sig函数发过去*/
    errno = save_errno; // 【类似于单片机的中断函数，所以需要还原之前的环境】
}

//设置信号函数 捕捉信号并发送
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler; // 指定信号处理函数
    if (restart)
        sa.sa_flags |= SA_RESTART; // 设置程序收到信号时，重新调用被该信号终止的系统调用
    sigfillset(&sa.sa_mask); // 在信号集中设置所有的信号【利用sigaction中的信号掩码】
    assert(sigaction(sig, &sa, NULL) != -1); // 去捕获sig信号，sa作为处理配置文件
}

// alarm()函数
// alarm开启定时器，时间到后给调用alarm的进程发送一个SIGALRM信号。 
// 默认情况下，会终止当前进程。
//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick(); // 触发一次心搏 获得
    alarm(m_TIMESLOT);  // 闹钟
}

// 显示错误
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

// 设置定时器函数
void cb_func(client_data *user_data) // 关闭连接减少用户数量
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}