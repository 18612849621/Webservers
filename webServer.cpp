#include "webServer.h"

WebServers::WebServers(int _port, string userName, string passWord): port(_port), m_userName(userName), 
    m_passWord(passWord){   // 构造函数【有参】
    // 对SIGPIE信号进行处理 [默认会终止进程，这里用SIG_IGN忽略]
    addsig(SIGPIPE, SIG_IGN);
    // 初始化
    init_log();
    init_threadpool();
    init_orm();
    init_tcp();
    init_epoll();
    init_pipe();
    // 设置定时器个数
    users_timer = new client_data[MAX_FD];
}

WebServers::~WebServers(){ // 析构函数
    close(epollfd);
    close(listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete [] users;
    delete[] users_timer;
    delete pool;
} 

// 线程池初始化
void WebServers::init_threadpool() {
    try {
        pool = new threadpool<http_conn>;
    }catch(...) { // 如果抓到异常则直接退出
        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    users = new http_conn[MAX_FD];
}
// 日志初始化
void WebServers::init_log() {
    m_close_log = 0;  // 用于控制是否开关日志系统 0 : 启用
    m_asny = 800; // 如果是0就是同步日志，大于0就是异步日志
    Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, m_asny); // 单例模式获取【懒汉】异步
}


// 初始化TCP模块
void WebServers::init_tcp() {
    ret = 0;
    listenfd = socket(PF_INET, SOCK_STREAM, 0); // 创建监听socket
    
    // // 设置端口复用
    int reuse = 1; // 为1代表复用
    // 复用可以从time_wait模式直接退出
    ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    assert(ret != -1);

    // 创建ipv4 socket地址 输入各项属性
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // socket命名
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    // 监听
    ret = listen(listenfd, 5); // logback = 5 说明最大可以监听请求数量为5左右
    assert(ret != -1);
}
// 初始化epoll相关
void WebServers::init_epoll(){
    epollfd = epoll_create(5); // 创建epoll内核事件表大小为5
    assert(epollfd != -1);

    // 将监听(listenfd)的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false); // 注意监听事件不可设置，这样会造成后续的客户请求无法收到
    // 注意监听listenfd如果是ET模式，需要对accpet进行循环使用，因为每一次epoll_wait()触发条件是需要监听对象有新变化才能就绪
    // 所以当同时有多个连接的时候，需要在通知有连接时，accpet建立所有连接，不然会造成TCP连接请求阻塞在监听socket的监听队列里
    // 导致一直无法建立连接，需要下一次来新的连接请求（监听socket状态改变）才可以。
    // 当然使用LT模式也无妨，不过会造成资源浪费
    // 将事件表初始化到http连接类
    http_conn::m_epollfd = epollfd;
}

void WebServers::init_pipe() {
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd); // 创建socket联通套接字
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(epollfd, m_pipefd[0], false, 0); // 设置信号来检测管道
    
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);
    //  unsigned int alarm(unsigned int seconds);
    //  -功能：设置定时闹钟，函数调用的时候开始计时，当倒计时为 0 的时候。
    //  函数会给当前的进程发送一个信号 SIGALARM
    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = epollfd;
}

//初始化mysql连接池
void WebServers::init_orm(){
    corm * orm = corm::getInstance();
    orm->init(m_userName, m_passWord, "yourdb", m_close_log);
    cout << "orm初始化完毕" << endl;
}

void WebServers::servers_start(){
    bool timeout = false;
    bool stop_server = false;
    while (!stop_server) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1); // num返回就绪事件的数量
        if ((num < 0) && (errno != EINTR)) { // 如果错误为EINTR说明读是由中断引起的
            LOG_ERROR("%s", "epoll failure");
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
                // 因为ET模式 所以设置connfd为非阻塞模式
                if ( connfd < 0 ) { // 如果出现了错误
                    LOG_ERROR("errno is: %d\n", errno);
                    printf( "errno is: %d\n", errno);
                    continue;
                } 

                if (http_conn::m_user_count >= MAX_FD) {
                    // 目前连接数满了
                    // 给客户端写一个信息：服务器正忙。
                    const char* meg = "Severs is busy!!";
                    // 发送套接字
                    send(sockfd, meg, strlen( meg ), 0 );
                    close(connfd);
                    continue;
                }
                LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                // 将新的客户的数据初始化，放到数组中[connfd根据连接数量递增，所以直接用作数组的索引]
                users[connfd].init(connfd, client_address, m_close_log);
                timer(connfd, client_address); // 绑定定时器
            }else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或者错误等事件
                users[sockfd].close_conn();
                // 定时器相关
                heap_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd); // 删除定时器
            }else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){ // 处理信号
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
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
        // 处理完所有的epoll事件 再处理过期的定时器任务
        if (timeout)
        {
            utils.timer_handler(); // 处理过期的定时器，任务重置定时器时间

            LOG_INFO("%s", "timer tick");

            timeout = false; // 重新恢复状态
        }
    }
}

void WebServers::addsig(int sig, void(handler)(int)) { // 添加信号捕捉
    struct sigaction sa; // 描述信号处理细节的结构体
    memset(&sa, '\0', sizeof(sa)); // 初始化
    sa.sa_handler = handler; // 绑定处理函数
    sigfillset(&sa.sa_mask); // 设置信号集掩码
    sigaction(sig, &sa, NULL); // 捕捉sig信号
}

void WebServers::timer(int connfd, struct sockaddr_in client_address) {
    // 初始化client_data数据
    // 创建定时器，设置回调函数函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    heap_timer * timer = new heap_timer(); // 创建一个新的定时器
    timer->user_data = &users_timer[connfd]; // 赋值用户数据
    timer->cb_func = cb_func; // 赋值定时器功能
    time_t cur = time(NULL); // 获得当时的时间
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer; // 绑定定时器
    utils.m_timer_lst.add_timer(timer); // 添加定时器
    
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在堆中进行调整
void WebServers::adjust_timer(heap_timer * timer) {
    time_t cur = time(NULL); // 获取当前时间
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer); // 将新的定时器进行调整

    LOG_INFO("adjust timer once");
}

void WebServers::deal_timer(heap_timer * timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]); // 触发定时器的函数
    if (timer) { // 如果是有的那就给他移除
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd); // 关闭了第几个连接
}

bool WebServers::dealwithsignal(bool &timeout, bool &stop_server) { // 统一事件源
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0); // 从管道中获取signal状态
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM: // 说明有过期的定时器
            {
                timeout = true;
                break;
            }
            case SIGTERM: // 说明需要退出
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}