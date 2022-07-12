#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>

using namespace std;

Log::Log() {
    m_count = 0;        // 日志行数记录
    m_is_async = false; // 是否同步标志位
}

Log::~Log() {
    if (m_fp != NULL) {
        // 如果还有文件指针 就关掉它
        fclose(m_fp);
    }
}

// 异步需要设置阻塞队列的长度，同步不需要设置
// 写入方式通过初始化时是否设置队列大小（表示在队列中可以放几条数据）来判断，若队列大小为0，则为同步，否则为异步。
bool Log::init(const char * file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    // 如果设置了max_queue_size == 0, 则设置为同步
    if (max_queue_size >= 1) {
        // 设置默认写入方式为异步
        m_is_async = true;
        m_stop = false;  // 启动等待线程用于承接日志队列任务
        // 创建并设置阻塞队列长度
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;

        // flush_log_thread为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    // 输出内容的长度
    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size]; // 建立缓冲区
    memset(m_buf, '\0', sizeof(m_buf)); // 初始化

    // 日志的最大长度
    m_split_lines = split_lines;

    time_t t = time(NULL); // 建立时间对象
    struct tm * sys_tm = localtime(&t); // 获取当前时间 指针
    struct tm my_tm = * sys_tm; // 获取当前的时间[值]
    /*-------------就是创建日志文件----------------*/
    // 指针p 指向 从后往前找到第一个/的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0}; // 文件名字

    // 相当于可以自动定义日志名字
    // 若输入的文件名没有/，则直接将时间+文件名作为日志名
    if (p == NULL) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }else {
        // 将/的位置后移一个位置，然后复制到logname中
        strcpy(log_name, p + 1);
        // p - filename + 1 是文件所在路径文件夹的长度
        // dirname相当于./
        strncpy(dir_name, file_name, p - file_name + 1);
        // 后面的参数跟format有关系
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    // "a" : 追加到一个文件。写操作向文件末尾追加数据。如果文件不存在，则创建文件。
    // 初始化日志文件【就是有时间的文件而已】
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL) {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec; // 获取秒
    struct tm * sys_tm = localtime(&t); // 获取当前时间 年月日
    struct tm my_tm = *sys_tm;  // 获取当前的时间[值]
    char s[16] = {0};

    // 日志分级 进行打印
    switch (level) {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }
    // 线程安全写入
    m_mutex.lock();
    
    // 更新现有行数
    m_count++;
    // 日志不是今天或写入的日志行数最大行的倍数 创建一个新的文件
    // m_split_lines为最大行数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_fp); // 将之前所有缓冲区的内容放进去
        fclose(m_fp); // 关掉当前文件指针
        char tail[16] = {0};
        
        // 格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        // 如果是时间不是今天
        // 创建今天的日志，更新m_today和m_count
        if (m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }else {
            // 超过了最大行，在之前的日志名基础上加上后缀，m_count/m_split_lines
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a"); // 创建新的文件
    }
    m_mutex.unlock();

    va_list valst;
    // 将传入的format参数赋值给valst，便于格式化输出
    va_start(valst, format);

    string log_str;
    m_mutex.lock(); // 上锁开始处理输入业务
    // 写入内容格式：时间 + 内容
    // 时间格式化，snprintf 成功返回写字符的总数，其中不包括结尾的null字符
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数（不包含终止符）
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    // 封口构成字符串
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    
    log_str = m_buf;
    m_mutex.unlock();
    // 若m_is_async 为true表示异步，默认为同步
    // 若异步，则将日志信息加入阻塞队列，同步则加锁向文件中写
    if (m_is_async && !m_log_queue->full()) { // 异步
        m_log_queue->push(log_str);
        m_logstat.post(); // 信号量增加
    }else { // 同步
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valst); // 结束
}

void Log::flush(void) { // 封装成线程安全的刷新
    m_mutex.lock();
    // 强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}