#pragma once

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../../lock/locker.h"
#include "../../log/log.h"

using namespace std;

class sql_connection_pool{
    public:
        MYSQL * GetConnection();                  //获取数据库连接
        bool ReleaseConnection(MYSQL* conn);      // 释放连接
        int GetFreeConn();                       // 获取连接
        void DestroyPool();                      // 销毁所有链接

        // 单例模式
        static sql_connection_pool * GetInstance();

        void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);
        int m_FreeConn;  // 当前空闲的连接数
    private:
        sql_connection_pool();
        ~sql_connection_pool();

        int m_MaxConn;   // 最大连接数
        int m_CurConn;   // 当前已使用的连接数
        // int m_FreeConn;  // 当前空闲的连接数
        locker lock;
        list<MYSQL *> connList; // 连接池 这里用链表实现 保证建立时的时间复杂度低
        sem reserve;

    public:
        string m_url;                  // 主机地址
        string m_Port;                 // 数据库端口号
        string m_User;                 // 登陆数据库用户名
        string m_PassWord;             // 登陆数据库密码
        string m_DatabaseName;         // 使用数据库名
        int m_close_log;               // 日志开关
};

// RAII的核心思想是将资源或者状态与对象的生命周期绑定，
// 通过C++的语言机制，实现资源和状态的安全管理,智能指针是RAII最好的例子
class connectionRALL{ // 将连接池对象封装成RALL 来操作他
    public:
        connectionRALL(MYSQL *& con, sql_connection_pool * connPool);
        ~connectionRALL();

    private:
        MYSQL * conRALL;
        sql_connection_pool * poolRALL;
};
