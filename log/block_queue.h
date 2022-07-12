#pragma once
/*************************************************************
*封装stl的queue实现的阻塞队列
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/
#include <iostream>
#include <pthread.h>
#include <sys/time.h>
#include <queue>
#include "../lock/locker.h"

using namespace std;

template <typename T>
class block_queue{
    public:
        block_queue(int max_size = 1000){// 构造函数
            if (max_size <= 0) { // 非法就退出
                exit(-1);
            }
            m_max_size = max_size; // 只需要初始化一下队列最大长度
        } 
        ~block_queue(); // 析构函数【stl自带析构不需要写】
        void clear(){//清空日志
            m_mutex.lock();
            queue<int> empty;
            swap(empty, m_list);
            m_mutex.unlock();
        } 
        bool full(){  // 判断队列是否满
            m_mutex.lock();
            if (m_list.size() >= m_max_size) {
                m_mutex.unlock();
                return true;
            }
            m_mutex.unlock();
            return false;
        } 
        bool empty(){ // 判断队列是否空
            m_mutex.lock();
            if (m_list.empty()) {
                m_mutex.unlock();
                return true;
            }
            m_mutex.unlock();
            return false;
        } 
        bool front(T & value){ // 返回队首元素
            m_mutex.lock();
            if (m_list.empty()) { // 别锁套锁效率低
                m_mutex.unlock();
                return false;
            }
            value = m_list.front(); // 获取队首元素
            m_mutex.unlock();
            return true;
        } 
        bool back(T & value) { // 返回队尾元素
            m_mutex.lock();
            if (m_list.empty()) { // 别锁套锁效率低
                m_mutex.unlock();
                return false;
            }
            value = m_list.back(); // 获取队首元素
            m_mutex.unlock();
            return true;
        } 
        int size() { // 返回队列长度
            int s = 0;
            m_mutex.lock();
            s = m_list.size();
            m_mutex.unlock();
            return s;
        } 
        int max_size() { // 获取队列的最大长度
            int ms = 0;
            m_mutex.lock();
            ms = m_max_size;
            m_mutex.unlock();
            return ms;
        } 
        /*往队列添加元素，需要将所有使用队列的线程先唤醒
        当有元素push进队列，相当于生产者生产了一个元素
        若当前没有线程等待条件变量，则唤醒无意义*/
        bool push(const T & item) {
            m_mutex.lock();
            if (m_list.size() >= m_max_size) {
                m_cond.broadcast(); // 因为太满了 求人来取
                m_mutex.unlock();
                return false; // 目前是满的所以错误
            }
            m_list.push(item);
            m_cond.broadcast(); // 装好了 求人来取
            m_mutex.unlock();
            return true;
        }
        // pop时 如果当前队列没有元素，将会等待条件变量
        bool pop(T &item) {
            m_mutex.lock();
            while (m_list.size() <= 0) { // 用while防止线程出现冲突
                // 拿着锁放入阻塞队列中，再把锁释放（可以供其他线程使用）
                // 因为封装过所以成功就是true[m_cond] 等待 signal or boardcast 来唤醒他
                if (!m_cond.wait(m_mutex.get())) {
                    m_mutex.unlock(); // 没成功就解锁
                    return false;
                }
            }
            // 先返回对应的值 在出栈
            item = m_list.front();
            m_list.pop();
            m_mutex.unlock();
            return true;
        }
    private:
        locker m_mutex;
        cond m_cond;
        queue<T> m_list;
        int m_max_size; // 整个队列的最大长度
};