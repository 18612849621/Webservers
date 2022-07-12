# include "log.h"
# include <iostream>
# include <string.h>
# include <unistd.h>
# include <pthread.h>

using namespace std;

int main() {
    int m_close_log = 0;
    int m_asny = 800;
    Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, m_asny); // 单例模式获取【懒汉】异步
    if (m_asny == 0){
        LOG_INFO("现在是同步日志");
    }else{
        LOG_INFO("现在是异步日志");
        
    }
    LOG_INFO("现在是异步日志1");
        LOG_INFO("现在是异步日志2");
        LOG_INFO("现在是异步日志3");
        LOG_INFO("现在是异步日志4");
        LOG_INFO("现在是异步日志5");
        LOG_INFO("现在是异步日志6");
    sleep(1);
    return 0;
}