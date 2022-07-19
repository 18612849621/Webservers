#include "sql_conn_pool.h"

sql_connection_pool::sql_connection_pool() {
    m_CurConn = 0;  // 当前已使用的连接数
    m_FreeConn = 0;   // 当前空闲的连接数
}

// 单例模式获得对象（懒汉模式）
sql_connection_pool * sql_connection_pool::GetInstance() { 
    // c++11 static保证线程安全 
    static sql_connection_pool connPool;
    return &connPool;
}

// 构造初始化
void sql_connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log) {
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;
    
    for (int i = 0; i < MaxConn; ++i) {
        MYSQL * con = NULL;
        con = mysql_init(con); // 初始化MYSQL句柄
        if (con == NULL) { // 没有成功打开mysql
            LOG_ERROR("Init MySQL Error!");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0); // 连接MySQL
        if (con == NULL) {
            LOG_ERROR("Connection MySQL Error!");
            exit(1);
        } 
        
        connList.push_back(con); // 放入把建立好的sql连接放入到连接池中
        ++m_FreeConn; // 当前空闲的连接数 + 1
    }

    reserve = sem(m_FreeConn); // 直接初始化信号量（按照一开始有的连接数）

    m_MaxConn = m_FreeConn; // 初始化的时候有多少free的就是最大多少
    cout << "目前有" << connList.size() << "个连接在连接池中" << endl;
}


// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL * sql_connection_pool::GetConnection() {
    MYSQL * con = NULL;

    if (0 == connList.size()) {
        return NULL;
    }

    reserve.wait(); // 信号量减少一个，如果没有了就阻塞

    lock.lock(); // 上锁使用线程安全

    con = connList.front(); // 获取第一个连接
    connList.pop_front();  // 剔除该连接
    if (con == NULL) {
        cout << "获取连接失败" << endl;
    }
    --m_FreeConn;       // 减少空闲连接数
    ++m_CurConn;        // 增加使用连接数
    lock.unlock();      // 解锁
    return con;  // 返回对应连接
}

// 释放当前使用的连接
bool sql_connection_pool::ReleaseConnection(MYSQL * con) {
    if (con == NULL) {
        return false;
    }
    // 如果得到了这个连接 那么就开始处理
    lock.lock();

    connList.push_back(con); // 放回链表中，因为不需要有序，所以不用队列
    ++m_FreeConn;  // 空闲数量++
    --m_CurConn;   // 正在使用的数量--

    lock.unlock();  // 解锁

    reserve.post();  // 增加信号量
    return true;
}

// 销毁数据库连接池
void sql_connection_pool::DestroyPool() {
    lock.lock();
    if (connList.size() > 0) { // 说明连接池大小不为0
        list<MYSQL* >::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            MYSQL * con = *it; // 使用指针获取对应连接
            mysql_close(con); // 断开连接api
        }
        m_CurConn = 0;  // 清空属性
        m_FreeConn = 0;  // 清空属性
        connList.clear();  // 清空所有连接
    }
    lock.unlock();
}

// 获取当前空闲的连接数
int sql_connection_pool::GetFreeConn(){
    return this->m_FreeConn;
}

sql_connection_pool::~sql_connection_pool(){
    DestroyPool(); // 使用刚才定义好的函数处理
}

connectionRAII::connectionRAII(MYSQL *& SQL, sql_connection_pool* connPool) {
    // 不用指针的引用的话就得用双指针，要不然这个指针本身的值是不会改变的就成了局部指针了没用
    // 核心：当指针本身作为参数需要进行处理时，想要从外部继承他的值就需要通过引用或者指针的指针来改变
    SQL = connPool->GetConnection(); // 通过指针的指针获取单例产生的对象的指针
    conRAII = SQL;  // 获取对应的对象
    poolRAII = connPool; // 获取对应的连接池
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII); // 把生成的数据库连接池对象删除
}