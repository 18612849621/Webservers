#pragma once

#include <pthread.h> // 多线程头文件
#include <exception> // 错误处理库
#include <semaphore.h> // 信号量头文件
// 线程同步机制封装类

// 互斥锁类 ： 可以理解为二进制的信号量
class locker {
    public:
        locker() {
            if (pthread_mutex_init(&m_mutex, NULL) != 0) { // 如果正确返回0
                throw std::exception();
            }
        }

        ~locker() {
            pthread_mutex_destroy(&m_mutex); // 销毁对象
        }

        bool lock() { // 以原子操作方式上锁
            return pthread_mutex_lock(&m_mutex) == 0;
        }

        bool unlock() { // 以原子操作方式解锁
            return pthread_mutex_unlock(&m_mutex) == 0;
        }
        // pthread_mutex_t 互斥锁使用特定的数据类型
        pthread_mutex_t * get() {  // 获取互斥锁
            return &m_mutex;
        }
    private:
        pthread_mutex_t m_mutex; // 定义互斥锁
};

// 条件变量类 : 判断队列中是否有数据，如果有数据那么线程唤醒进行处理
class cond {
    public:
        cond() {
            if (pthread_cond_init(&m_cond, NULL) != 0) {
                throw std::exception();
            }
        }

        ~cond() {
            pthread_cond_destroy(&m_cond);
        }
        
        bool wait(pthread_mutex_t * mutex) { // 判断当前互斥锁是否可用
            int ret = 0;
            pthread_mutex_lock(mutex);
            ret = pthread_cond_wait(&m_cond, mutex);  // 阻塞方式获取条件变量[加锁安全]
            pthread_mutex_unlock(mutex);
            return ret == 0;
        }

        bool timedwait(pthread_mutex_t * mutex, const struct timespec * t) {
            return pthread_cond_timedwait(&m_cond, mutex, t) == 0; // 指定阻塞时间获取条件变量
        }

        bool signal() { 
            return pthread_cond_signal(&m_cond) == 0; // 发送满足条件信号量
        }

        bool broadcast() {
            return pthread_cond_broadcast(&m_cond) == 0; // 调用此函数将唤醒所有等待cond条件变量的线程。
        }
    private:
        pthread_cond_t m_cond;
};

// 信号量类
class sem {
    public:
        sem() { // 初始化信号量为0
            if (sem_init(&m_sem, 0, 0) != 0) {
                throw std::exception();
            }
        }

        sem(int num) { // 带值初始化信号量
            if (sem_init(&m_sem, 0, num) != 0) {
                throw std::exception();
            }
        }

        ~sem() {
            sem_destroy(&m_sem);
        }

        // 以原子的方式将信号量的值减1，如果信号量为0则sem_wait就会阻塞，直到他具有非0值
        bool wait() {
            return sem_wait(&m_sem) == 0;
        }
        // 以原子的方式将信号量的值增加1，当信号量的值大于0时，其他正在调用sem_wait等待信号量的线程将被唤醒
        bool post() {
            return sem_post(&m_sem) == 0;
        }
    private:
        sem_t m_sem;
};