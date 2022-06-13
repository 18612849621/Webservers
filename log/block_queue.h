#pragma once
/*************************************************************
*双循环队列实现的阻塞队列
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/
#include <iostream>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
#include "cycle_linkedlist.h"

using namespace std;

template <typename T>
class block_queue{
    public:
        block_queue(int max_size = 1000); // 构造函数
        ~block_queue(); // 析构函数
        void clear(); //清空日志
        bool full(); // 判断队列是否满
        bool empty(); // 判断队列是否空
        bool front(T & value); // 返回队首元素
        bool back(T & value); // 返回队尾元素
        int size(); // 返回循环队列长度
        int max_size(); // 获取循环队列的最大长度
        /*往队列添加元素，需要将所有使用队列的线程先唤醒
        当有元素push进队列，相当于生产者生产了一个元素
        若当前没有线程等待条件变量，则唤醒无意义*/
        bool push(const T & item); 
        // pop时 如果当前队列没有元素，将会等待条件变量
        bool pop(T &item);
    private:
        locker m_mutex;
        cond m_cond;
        cycle_linkedlist<T>* m_list;
        int m_size;
        int m_max_size;
        int m_front;
        int m_back;
};