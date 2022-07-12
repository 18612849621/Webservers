CXX ?= g++
CXXFLAGS ?= -g
server: main.cpp ./httpconnection/http_conn.cpp ./log/log.cpp webServer.cpp ./corm/mysql_connpool/sql_conn_pool.cpp ./corm/corm.cpp ./timer/timer.cpp
		$(CXX) -o $@ $^ -lpthread -lmysqlclient