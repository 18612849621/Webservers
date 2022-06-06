#include "http_conn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

int http_conn::m_epollfd = -1; // 所有socket上的事件都被注册到同一个epoll事件中
int http_conn::m_user_count = 0; // 统计用户的数量

// 网站的根目录
const char* doc_root = "/home/parallels/Desktop/Parallels Shared Folders/Home/Desktop/WebServer/resources";

int setnonblocking(int fd) { // 设置文件描述符非阻塞
    int old_option = fcntl(fd, F_GETFL);  // 获取旧设置
    int new_option = old_option | O_NONBLOCK; // 设置非阻塞
    fcntl(fd, F_SETFL, new_option); // 配置新设置
    return old_option; // 返回旧状态
}

// 添加文件描述符到epoll中
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; // 将fd 注册可读 以及 关闭连接提示事件 到事件内核表中，只要在就绪内核中判断类型就可以了
    if (one_shot) { // 注意监听事件不可设置，这样会造成后续的客户请求无法收到
        event.events |= EPOLLONESHOT; // 无论LT or ET 仅允许一个线程注册一个事件，防止多线程处理单一事件[文件描述符]
    }
    // EPOLLONESHOT 事件如果不用epoll_ctl进行重置，该事件将不会被触发
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); // 设置epoll属性
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中删除文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符[最主要就是重制socket上EPOLLONESHOT事件，以确保下一次，EPOLLIN事件能被触发]
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP; // 默认设置不变
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::init(){ // 初始化连接的状态机相关配置
    m_check_state = CHECK_STATE_REQUESTLINE; // 主状态机初始化状态为解析请求首行
    m_checked_idx = 0; // 初始化当前正在分析的字符在读缓冲区的位置
    m_start_line = 0; // 初始化当前正在解析的行的起始位置
    m_read_idx = 0; // 初始化标识读缓冲区中以及读入的客户端数据的最后一个字节的下一个位置[为了方便一次性读完]
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;
    
    bytes_to_send = 0;
    bytes_have_send = 0; 
    
    m_write_idx = 0;  // 初始化写缓冲区的开始指针
    bzero(m_read_buf, READ_BUFFER_SIZE); // 清空读缓冲区
    bzero(m_write_buf, WRITE_BUFFER_SIZE); // 清空写缓冲区
    bzero(m_real_file, FILENAME_LEN); // 清空获取文件的路径名字
}

void http_conn::init(int sockfd, const sockaddr_in & addr){ // 初始化新接受的连接，配置用户信息
    m_sockfd = sockfd;
    m_address = addr;

    // 设置端口复用
    int reuse = 1; // 为1代表复用
    int ret = setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    assert(ret != -1);

    // 添加到epoll对象中
    addfd(m_epollfd, m_sockfd, true); // 因为是普通的工作事件可以ONESHOT
    m_user_count++; // 用户个数增加

    init(); // 再次把连接相关的系统配置初始化
}

// 关闭一个连接
void http_conn::close_conn() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd); // 将注册socket移除
        m_sockfd = -1; // 设置-1就失效了
        m_user_count--; // 关闭一个连接，用户总数量-1
    }
}

// 循环读取客户数据，直到无数据或者对方关闭连接
bool http_conn::read(){ // 非阻塞的读
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false; // 如果超出缓冲区大小，则失败
    }

    // 读取到的字节
    int byte_read = 0;
    while (true) { 
        // recv return: the number of bytes received 第三个参数时 缓冲区的最大尺寸
        byte_read = recv(m_sockfd, m_read_buf + m_read_idx, 
        READ_BUFFER_SIZE - m_read_idx, 0);
        if (byte_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞读完所有[无数据了]
                break;
            }
            return false; // 如果不是没有数据了 那就是出错了
        }else if (byte_read == 0) {
            // 对方关闭连接
            return false;
        }
        m_read_idx += byte_read;  // 更新数据的索引
    }
    // printf("读取到了数据:%s\n", m_read_buf);
    return true;
} 
bool http_conn::write(){ // 非阻塞的写
    int temp = 0;
    
    if ( bytes_to_send == 0 ) { 
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN );  // 刷新一下oneshot模式下m_sockfd的属性
        init(); // 该线程初始化，重新准备下一次的连接请求
        return true;
    }

    while(1) {
        // 集中写 ｜ 分散读
        // m_iv 代表的是内存块的信息
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) { // 说明没法继续读了，但不是出错[非阻塞模式]
                modfd( m_epollfd, m_sockfd, EPOLLOUT ); // 重新注册写事件
                return true;
            }
            // 说明出错误了，把对应内存的数据清空掉
            unmap();
            return false;
        }
        // temp = return the number of bytes read
        bytes_have_send += temp; // 找到下一个要发送的开始位置
        bytes_to_send -= temp; // 还需要发送多少的字节[当前需要发送的总长度]

        if (bytes_have_send >= m_iv[0].iov_len) // 已经把状态行 + 响应头部发送完毕，外加可能有部分的响应内容
        {
            m_iv[0].iov_len = 0; // 第一部分的长度就设置为0了[等于不要了]
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx); // 跳过已经发送过的请求内容部分
            m_iv[1].iov_len = bytes_to_send; // 直接用更新过后的len，因为内容的长度已经改变了
        }
        else
        {   // 还没有将状态行 + 响应头部部分完全处理完，所以响应内容部分不需要改变
            m_iv[0].iov_base = m_write_buf + bytes_have_send; // 更新新的写位置
            m_iv[0].iov_len = m_iv[0].iov_len - temp;  // 更新一下状态行 + 响应头部部分需要写的长度
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            // 该线程重新负责起读事件
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            // 如果是长连接的那就初始化
            if (m_linger)
            {
                init();
                return true;
            }
            else // 如果不是就直接false退出
            {
                return false;
            }
        }

    }
}  
// PS : 类内枚举需要加上类作用域【如果作为返回值】
// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read() { // 解析HTTP请求
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char * text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
        || ((line_status = parse_line()) == LINE_OK)) {
        // 如果主状态机是解析请求体，且行解析状态为读取到一个完整的行 那可以执行循环

        // 当前的行解析状态为读取到一个完整的行，那我也可以处理
        text = get_line(); // 获取当前行的字符串内容
        m_start_line = m_checked_idx; // m_checked_idx 由于parse_line()更新到\r\n的下一个，也就是下一行的开始
        // 用于查看当前的处理状态
        std::string MMap[] = {"CHECK_STATE_REQUESTLINE", "CHECK_STATE_HEADER", "CHECK_STATE_CONTENT"};
        std::cout << "[" << MMap[0] << "]: ";
        printf("got 1 http line : %s\n", text);

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE :  // 请求首行状态
            {
                ret = parse_request_line(text); // 根据当前任务分析出HTTP请求的可能结果
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;  // 继续下一行的解析
            }

            case CHECK_STATE_HEADER :  // 请求头部状态
            {
                ret = parse_headers(text); // 根据当前任务分析出HTTP请求的结果
                if (ret == BAD_REQUEST) {  // 错误信息就不要继续解析了
                    return BAD_REQUEST;
                }else if (ret == GET_REQUEST) { // 头部请求分析完认为获得一个完整的客户请求
                    return do_request(); // 解析具体的请求内容
                }
                break; // 继续下一行的解析
            }

            case CHECK_STATE_CONTENT : // 请求内容状态
            {
                ret = parse_content(text); 
                if (ret == GET_REQUEST) { // 头部请求分析完认为获得一个完整的客户请求
                    return do_request(); // 解析具体的请求内容(书接上一个状态，一起解析内容体部分)
                }
                // 没有成功解析客户请求内容体
                line_status = LINE_OPEN; // 行数据尚不完整
                break;  // 继续下一行的解析
            }

            defualt :
            {
                return INTERNAL_ERROR; // 表示服务器内部错误
            }
        }
    }
    return NO_REQUEST;
} 
// 按照内容分三类单独处理
http_conn::HTTP_CODE http_conn::parse_request_line(char * text) { // 解析请求首行
    // 解析HTTP请求行，获得请求方法，目标URL，HTTP版本
    //    [m_url]
    // GET      / HTTP/1.1
    m_url = strpbrk(text, " \t"); // strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针。
    // GET\0/ HTTP/1.1
    *m_url++ = '\0'; // 封口
    char * method = text;
    // strcasecmp用忽略大小写比较字符串，通过strcasecmp函数可以指定每个字符串用于比较的字符数，
    // strcasecmp用来比较参数s1和s2字符串前n个字符，比较时会自动忽略大小写的差异。
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET; // 说明GET解析正确，然后赋值到任务
    }else {
        return BAD_REQUEST;
    }

    // 获取HTTP协议版本     [m_url]
    // /(这里可能有也可能没有)      HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    //                      [m_version]
    // /(这里可能有也可能没有)\0    H      TTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {  // 这里没有存下协议的版本号 只是判断了
        return BAD_REQUEST;
    }
    // 如果版本号是正确的
    // http://192.168.1.1:10000/index.html
    
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7; // 到了这里 192.168.1.1:10000/index.html
        // strchr函数功能为在一个串中查找给定字符的第一个匹配之处。
        m_url = strchr(m_url, '/'); // /index.html
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    
    m_check_state = CHECK_STATE_HEADER; // 主状态机检查状态变成检查请求头

    return NO_REQUEST; // 还需要继续解析
}
http_conn::HTTP_CODE http_conn::parse_headers(char * text) { // 解析请求头部
    // 遇到空行，标识头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        // 状态机转换到CHECK_STATE_CONTENT状态
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST; // 还需要继续读去
        }
        // 否则就当完成了HTTP请求
        return GET_REQUEST;
    }else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection头部字段 Connection: keep=alive
        text += 11; // 找一下一组命令
        text += strspn(text, " \t");
        if (strcasecmp(text , "keep=alive") == 0) {
            m_linger = true; // 如果设置为keep=alive那么就是保持连接
        }
    }else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t"); // 通通都越过找到内容对应索引
        m_content_length = atol(text);
    }else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }else {
        printf("oop! unknow header %s\n", text); // 只判断自己想要的这几个头
    }
    return NO_REQUEST;
} 
http_conn::HTTP_CODE http_conn::parse_content(char * text) { // 解析请求内容
    // 这里没有具体的解析HTTP请求的消息体，只是判断他是否是被完整度入的
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        // 这里相当于完全把内容页给跳过了，直接看看缓冲区的最后一位索引（也就是下一页的开始）
        // 与内容 + （请求首行 + 请求头）== m_checked_idx 的值是否被当前缓冲区存下了
        text[m_content_length] = '\0'; // 封口
        return GET_REQUEST;  // http请求结束
    }
    return NO_REQUEST;
} 

http_conn::LINE_STATUS http_conn::parse_line() { // 具体解析每一行的内容 将所有的内容 判断是\r\n 然后转换成\0\0制作字符串结尾 以[制作]出每一行
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx]; // 读取当前缓冲区指定位置的内容
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) { 
                return LINE_OPEN; // 行数据没有读取完整[因为正常情况m_checked_idx + 2才是m_read_index]
            }else if (m_read_buf[m_checked_idx + 1] == '\n') { // 遇到\r\n -> 转换成\0\0 因为字符串结束为\0
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                // 注意此时m_checked_idx对应的位置为\r\n的下一个，也就是下一行的开始
                return LINE_OK;
            }
            // 那如果都不是上面的那些情况，肯定是报文出错误了!
            return LINE_BAD;
        }else if (temp == '\n') { 
            // 比如上一次没有完全读取完还剩\n没有读取的情况下，
            // 我这边下一波第一个就会读取\n然后是下一行的内容，反向找一个整行请求
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                // 制作字符串结尾
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx] = '\0';
                return LINE_OK;
            }
            return LINE_BAD; // 不是特殊情况那肯定错了
        }
        // 没有就正常处理
    }
    return LINE_OPEN; // 没有达到LINE_OK 然后正常退出的话 那就是还需要继续处理
}  
// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
    // /home/parallels/Desktop/Parallels Shared Folders/Home/Desktop/WebServer/resources/index.html
    strcpy(m_real_file, doc_root); // 把根目录先copy过去
    int len = strlen(doc_root); // 为了下一步拼接做准备
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // stat 获取文件的相关状态 -1 fail 0 success
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE; // 没找到资源
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) { // 判断是否有读的权限
        return FORBIDDEN_REQUEST; // 如果禁止就返回禁止访问
    }

    // 判断是否为目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        // 是目录的话 那就是有错误的
        return BAD_REQUEST; // 表示客户请求语法错误
    }

    // 按照只读的方式打开文件
    int fd = open(m_real_file, O_RDONLY); // 文件描述符指向网页
    // 创建内存映射[相当于将文件从存储设备掉到内存中使用]
    // mmap将一个文件或者其它对象映射进内存。文件被映射到多个页上，
    // 如果文件的大小不是所有页的大小之和，最后一个页不被使用的空间将会清零。
    // mmap在用户空间映射调用系统中作用很大。
    m_file_address = (char* )mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close( fd );
    return FILE_REQUEST; // 文件请求,获取文件成功
}

// 对内存映射区执行munmap操作（相当于释放内存映射区的内容）
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
    }
}
// 以下这一组函数被process_write调用以填充HTTP应答。

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) { // 如果当前的写索引大于缓冲区大小
        return false;
    }
    va_list arg_list; // 解析多参数 
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

void http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
// 以上这一组函数被process_write调用以填充HTTP应答。

// 写HTTP响应
bool http_conn::process_write(HTTP_CODE ret){ // 根据服务器处理HTTP请求结果，决定返回给客户端的内容
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            // 添加状态行 和 响应头部信息
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf; // [状态行 和 响应头部]存储在写内存缓冲区的起始地址
            m_iv[ 0 ].iov_len = m_write_idx;  // 这个是内存缓冲区的长度
            m_iv[ 1 ].iov_base = m_file_address; // 这个是文件内容的起始地址
            m_iv[ 1 ].iov_len = m_file_stat.st_size; // 这个是文件内容的长度
            m_iv_count = 2; // m_iv_count表示被写内存块的数量

            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;
        default:
            return false;
    }
    // 如果不是以上的情况只输出上面的状态行 和 响应头部
    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
} 

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {  // NO_REQUEST : 请求不完整，需要继续读取客户数据
        modfd(m_epollfd, m_sockfd, EPOLLIN); // 没读完就重新注册读接着读
        return;
    }

    // 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
    }
    // 因为是oneshot模式所以每操作一个文件描述符需要重新设置功能，所以这里
    // 代表的是成功，然后需要我们去处理EPOLLOUT事件
    modfd( m_epollfd, m_sockfd, EPOLLOUT);
}
