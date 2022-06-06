CXX ?= g++
CXXFLAGS ?= -g
server: main.cpp ./httpconnection/http_conn.cpp
		$(CXX) -o $@ $^ -pthread