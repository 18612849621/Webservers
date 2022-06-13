# include "cycle_linkedlist.h"
# include <iostream>
# include <string.h>

using namespace std;

int main() {
    string str = "我爱你123";
    // 
    cycle_linkedlist<string> * cycle_list = new cycle_linkedlist<string>(2);
    cycle_list->get_linkedlen();
    cycle_list->get_queuelen();
    cycle_list->push("谁是大帅比");
    cycle_list->push("哦！");
    cycle_list->pop(str);
    cycle_list->push("潘禹含是大帅比！");
    cycle_list->get_queuelen();
    cycle_list->print_list();
    cycle_list->pop(str);
    cycle_list->get_queuelen();
    delete cycle_list;
    
    cout << "i'm finish!" << endl;
}