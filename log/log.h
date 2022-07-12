#pragma once

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;
/* 其实，C++0X以后，要求编译器保证内部静态变量的线程安全性，故C++0x之后该实现是线程安全的，
C++0x之前仍需加锁，其中C++0x是C++11标准成为正式标准之前的草案临时名字。
所以，如果使用C++11之前的标准，还是需要加锁，这里同样给出加锁的版本。
*/
class Log {
    public:
    //C++11以后,使用局部变量懒汉不用加锁
        static Log *get_instance(){ 
            static Log instance;
            return &instance;
        }

        // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
        bool init(const char * file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
        
        // 异步写日志公有方法，调用私有方法async_write_log
        static void *flush_log_thread(void * args) {
            Log::get_instance()->async_write_log(); // 静态函数必须手动调用，没有this指针
        }
        
        // 将输出内容按照标准格式整理
        void write_log(int level, const char * format, ...);

        // 强制刷新缓冲区
        void flush(void);

    private:
        Log();
        virtual ~Log();

        // // 异步写日志方法 [原版有问题]
        // void *async_write_log(){
        //     string single_log; // 每一条日志
        //     //从阻塞队列中取出一条日志内容，写入文件
        //     while (m_log_queue->pop(single_log)) {
        //         m_mutex.lock();
        //         // str，一个数组，包含了要写入的以空字符终止的字符序列。
        //         // stream，指向FILE对象的指针，该FILE对象标识了要被写入字符串的流。
        //         // c_str(): string-->char*
        //         // 生成一个const char*指针，指向以空字符终止的数组
        //         fputs(single_log.c_str(), m_fp);
        //         m_mutex.unlock();
        //     }
        // }


        // 异步写日志方法  [我的没问题]
        void *async_write_log(){
            string single_log; // 每一条日志
            //从阻塞队列中取出一条日志内容，写入文件
            while (!m_stop) {
                m_logstat.wait();
                m_mutex.lock();
                // str，一个数组，包含了要写入的以空字符终止的字符序列。
                // stream，指向FILE对象的指针，该FILE对象标识了要被写入字符串的流。
                // c_str(): string-->char*
                // 生成一个const char*指针，指向以空字符终止的数组
                m_log_queue->pop(single_log); // 获取当前需要写入内容
                fputs(single_log.c_str(), m_fp);
                m_mutex.unlock();
            }
        }

    private:
        char dir_name[128]; // 路径名
        char log_name[128]; // log文件名
        int m_split_lines;  // 日志最大行数
        int m_log_buf_size; // 日志缓冲区大小
        long long m_count;  // 日志行数记录
        int m_today;        // 按天分文件，记录当前时间是哪一天
        FILE *m_fp;         // 打开log的文件指针
        char *m_buf;        // 要输出的内容
        block_queue<string> * m_log_queue;  // 阻塞队列[链表实现]
        bool m_is_async;                    // 是否同步标志位 async异步
        locker m_mutex;                     // 互斥锁
        int m_close_log; //关闭日志
        int m_stop; // 用来一直启用异步队列
        
        sem m_logstat;  // 信号量：用来判断是否有异步输出
};

// 这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
// __VA_ARGS__宏前面加上##的作用在于，当可变参数的个数为0时，
// 这里printf参数列表中的的##会把前面多余的","去掉，否则会编译出错，建议使用后面这种，使得程序更加健壮
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}