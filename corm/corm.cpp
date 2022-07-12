#include "corm.h"

corm::corm() {

}

corm::~corm() {

}

corm* corm::getInstance() { // 单例写法./s
    static corm obj;
    return &obj;
}

void corm::init(string userName, string PassWord, string DataBaseName, int close_log){ // 初始化orm
    // 初始化databaseName的连接池
    m_userName = userName;
    m_passWord = PassWord;
    m_databaseName = DataBaseName;
    m_close_log = close_log;
    m_port = 3306; // sql使用默认端口号3306【个人推测应该是使用了端口复用 要不然不可能所有连接池用同一个端口】
    m_sql_num = 4; // 机器是4核的就先开这么大 sql连接池大小

    m_sql_connPool = sql_connection_pool::GetInstance(); // 获取单例
    m_sql_connPool->init("localhost", m_userName, m_passWord, m_databaseName, m_port, m_sql_num, m_close_log);

}

void corm::get_users_info(map<string, string> &usersInfo){
    // 先从连接池中获取一个mysql连接
    MYSQL * mysql = NULL;
    connectionRALL mysqlRALL(mysql, m_sql_connPool); // RALL机制获得连接 函数过后又释放连接

    // 在user表中检索username, passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username, passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 通过句柄从表中检索完整的结果集
    MYSQL_RES * result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    // 返回所有字段结构的数组
    MYSQL_FIELD * fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        // 一行里就两个 一个用户名 一个密码
        // row[0] 为用户名 row[1] 为密码
        string temp1(row[0]);
        string temp2(row[1]);
        usersInfo[temp1] = temp2;
    }

}

int corm::insert_user(string name, string password) {
    // 先从连接池中获取一个mysql连接
    MYSQL * mysql = NULL;
    connectionRALL sqlRALL(mysql, m_sql_connPool);
    // 开始执行插入操作
    char * sql_insert = (char *)malloc(sizeof(char) * 200);
    strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
    strcat(sql_insert, "'");
    strcat(sql_insert, name.c_str());
    strcat(sql_insert, "', '");
    strcat(sql_insert, password.c_str());
    strcat(sql_insert, "')");
    int res = mysql_query(mysql, sql_insert); // 0 成功 1 失败
    return res;
}