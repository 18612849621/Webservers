#pragma once
#include "./mysql_connpool/sql_conn_pool.h"
#include <map>

class corm {
    public:
        // 单例模式
        static corm * getInstance(); // 单例获得对象
        void init(string userName, string PassWord, string DataBaseName, int close_log);
        void get_users_info(map<string, string> &usersInfo);
        int insert_user(string name, string password); // name 用户的名字 password 用户的密码
    private:
        corm();
        ~corm();
        sql_connection_pool * m_sql_connPool; // sql连接池
        

    private:
        int m_port;                 // 数据库端口号
        string m_userName;                 // 登陆数据库用户名
        string m_passWord;             // 登陆数据库密码
        string m_databaseName;         // 数据库名字
        int m_sql_num; // 机器是4核的就先开这么大 sql连接池大小
        int m_close_log;               // 日志开关
};