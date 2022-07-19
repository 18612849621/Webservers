#pragma once

#include <pthread.h>
#include <list> // Lists将元素按顺序储存在链表中. 与 向量(vectors)相比, 它允许快速的插入和删除，但是随机访问却比较慢.
#include <exception>
#include <cstdio>
#include "../lock/locker.h"

// 线程池类，定义成模版类为了代码的复用, 模版参数T是任务类
template<typename T>
class threadpool { // 线程池类
    public:
        threadpool(int thread_number = 8, int max_requests = 65535);  // 因为本机器是4核的
        ~threadpool();
        bool append(T* request); // 添加任务
    
    private:
        static void* worker(void * arg); // 工作线程运行的函数，它不断从工作队列取出任务并执行
        void run(); // 执行函数

    private:
    // 线程的数量
    int m_thread_number;

    // 线程池数组, 大小为m_thread_number;
    pthread_t * m_threads;

    // 请求队列中最多允许的、等待处理的请求数量
    int m_max_requests;

    // 请求队列
    std::list< T* > m_workqueue;

    // 互斥锁
    locker m_queuelocker;

    // 信号量：用来判断是否有任务需要处理
    sem m_queuestat;

    // 是否结束线程
    bool m_stop;
};

template<typename T> // 定义构造函数
threadpool<T>::threadpool(int thread_number, int max_requests) :
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL) {
        // 如果初始化值非法则异常
        if ((thread_number <= 0) || (max_requests <= 0)) {
            throw std::exception();
        }
        m_threads = new pthread_t[m_thread_number];
        if (!m_threads) { // 若未成功创建线程池处理异常
            throw std::exception();
        }

        // 创建m_thread_number个线程，并将他们设置成线程脱离
        for (int i = 0; i < m_thread_number; ++i) {
            printf("create the %dth thread\n", i + 1);
            // 参数1 线程数组对应指针 参数3 线程入口函数地址必须是静态函数 参数4 将this指针传入worker可以让其使用非静态成员变量
            if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
                // 若有异常需要将所有的结果进行重制
                delete [] m_threads;
                throw std::exception();
            }
            // 创建出pthread线程后，如果线程需要重复创建使用，需要设置pthread线程为detach模式线程脱离
            // 如果既不需要第二个线程向主线程返回信息，也不需要主线程等待它，可以设置分离属性，创建“脱离线程”
            // 这样就实现了线程池
            if (pthread_detach(m_threads[i])) {
                // 一个有问题所有都不要
                delete [] m_threads; 
                throw std::exception();
            }
        }
    }

template<typename T>
threadpool<T>::~threadpool() {
    delete [] m_threads;
    m_stop = true; 
}

template<typename T>
bool threadpool<T>::append(T * request) {
    // 操作工作队列时一定要加锁，因为他被所有的线程共享
    m_queuelocker.lock(); 
    if (m_workqueue.size() > m_max_requests) { // 说明资源分配枯竭
        m_queuelocker.unlock(); // 线程解锁
        return false;
    }
    // 添加任务
    m_workqueue.push_back(request); // 加入请求队列
    m_queuelocker.unlock(); // 线程解锁

    m_queuestat.post(); // 更新信号量状态（增加信号量，为了判别是否有可以处理任务的资源）
    return true;
}

template<typename T>
void* threadpool<T>::worker(void * arg) {  // arg此时传入this指针
    // 所以为了使用当前的功能，需要将类型强转换
    threadpool * pool = (threadpool*) arg;
    pool->run();
    return pool; //没意义
}

template<typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait(); // 如果信号量有值，这里不阻塞；反之阻塞。信号P操作
        // 此时被唤醒加互斥锁
        m_queuelocker.lock(); //上锁
        if (m_workqueue.empty()) { // 说明无数据
            m_queuelocker.unlock(); // 解锁
            continue;
        }
        // 从请求队列中获取第一个任务
        // 将任务从请求队列删除
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock(); // 解锁

        if (!request) {
            continue;
        }

        request->process(); // 运行该任务类
    }
}
