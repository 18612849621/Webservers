// 使用循环链表 需要头尾巴指针
#pragma once

#include <iostream>
#include <assert.h>

using namespace std;

template <typename T>
class cycle_linkedlist { // 外部就单独设立指针
    private:
        struct linkednode { // 节点类
            linkednode * next;
            T content; // 记录内容
            bool FULL; // 记录是否已经填满
        };
        // 循环链表的表头表尾
        linkednode * link_head = nullptr;
        linkednode * link_tail = nullptr;
        struct ptr {
            // 用于标记对头队尾
            linkednode * q_head;
            linkednode * q_tail;
        };

    private:
        int linkedlist_len = 0; // 记录链表的长度 也是最大队列长度 - 1
        int queue_len = 0; // 记录队列的长度

    public:
        cycle_linkedlist(int len = 1000); // 有参构造函数构造出len长度的初始链表长度
        ~cycle_linkedlist();
        bool push(const T & val); // 向循环队列中插入
        bool pop(T & val); // 从队尾踢出

    public: // 检测用函数 之后就删除
        void get_linkedlen(); 
        void get_queuelen();
        void print_list();
    public:
        ptr* pointer = new ptr; // 用来给别的对象调用使用
    
};

template <typename T> // 定义构造函数
cycle_linkedlist<T>::cycle_linkedlist(int len) : linkedlist_len(len) {
    assert(len > 1);
    cout << "start create linkedlist!!" << endl;
    for (int i = 0 ; i < len; ++i) { // 因为也没有数据选择尾插法简单
        if (i == 0) {
            linkednode * new_node = new linkednode;
            link_head = new_node;
            link_tail = new_node;
            link_tail->next = link_head; // 尾巴接到头
            pointer->q_head = new_node;
            pointer->q_tail = new_node;
        }else {
            linkednode * new_node = new linkednode;
            link_tail->next = new_node;
            link_tail = new_node;
            link_tail->next = link_head;
        }
    }
    cout << "finished create " << linkedlist_len << " linkedlist!!" << endl;
}

template <typename T> // 定义构造函数
cycle_linkedlist<T>::~cycle_linkedlist(){
    cout << "start destroy!!" << endl;
    for (int i = 0; i < linkedlist_len; ++i) {
        linkednode* next = link_head->next;
        delete link_head; // 删除对应节点
        link_head = next;
    }
    cout << "it is destory!!" << endl;
}

template <typename T>
void cycle_linkedlist<T>::get_linkedlen() {
    linkednode* t = link_head;
    int lens = 1;
    while (t != link_tail) {
        lens++;
        t = t->next;
    }
    cout << "目前循环链表的长度为： " << lens << endl;
}

template <typename T>
void cycle_linkedlist<T>::get_queuelen() {
    linkednode * t = pointer->q_head;
    int lens = 0;
    while (t != pointer->q_tail) {
        lens++;
        t = t->next;
    }
    cout << "目前队列的长度为： " << lens << endl;
}

// 尾进头出
template <typename T>
bool cycle_linkedlist<T>::push(const T & val){  // 向队列中插入内容
    if (pointer->q_tail->next == pointer->q_head) { // 循环队列满的条件
        cout << "queue is FULL!!" << endl;
        return false;
    }
    // 向循环队列中插入
    pointer->q_tail->content = val;
    pointer->q_tail = pointer->q_tail->next;
    return true;
}

template <typename T>
bool cycle_linkedlist<T>::pop(T & val){
    if (pointer->q_tail == pointer->q_head) { // 循环队列空的条件
        cout << "queue is NULL!!" << endl;
        return false;
    }
    val = pointer->q_head->content;
    pointer->q_head = pointer->q_head->next;
    return true;
} // 从队尾踢出并获得内容

template <typename T>
void cycle_linkedlist<T>::print_list() {
    linkednode* t = pointer->q_head;
    while (t != pointer->q_tail) {
        cout << "->" << t->content;
        t = t->next;
    }
    cout << endl;
}