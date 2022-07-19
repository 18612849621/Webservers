#include "webServer.h"
#include <map>

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("按照如下格式运行: %s port_number\n", basename(argv[0])); // basename获取路径的最顶层文件名字
        exit(-1);
    }
    // 数据库连接信息
    string userName = "pan";
    string passWord = "123456";
    WebServers* server = new WebServers(atoi(argv[1]), userName, passWord);
    server->servers_start();
    return 0;
}
