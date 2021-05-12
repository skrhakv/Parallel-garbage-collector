#include <functional>
#include <iostream>
#include "gc.h"


class Node : public gc_object
{
public:
    int val;
    Node* left { nullptr };
    Node* right { nullptr };

    Node(int val) : val(val) {}

    ~Node() {
        std::cout << "Deleted:" << val << std::endl;
    }

protected:
    void get_ptrs(std::function<void (gc_object*)> callback) override {
        callback(left);
        callback(right);
    }
};

int main() {
{
     std::cout << "START" <<  std::endl;
    gc_root_ptr<Node> treeRoot = new Node(42);
    treeRoot->left = new Node(41);
    treeRoot->right = new Node(43);

    treeRoot->right = new Node(44);
    gc::collect();  // "Deleted: 43"
    std::cout << std::endl;
    // treeRoot.reset();
    // if(!treeRoot){
    //     std::cout << "correct! treeroot is nullptr" << std::endl;
    //     std::cout << treeRoot.get() << std::endl;
    // }
    // else{
    //     std::cout << "shiiit man" << std::endl;
    // }
}

    gc::collect();  // "Deleted: 42", "Deleted: 41", "Deleted: 44"
    std::cout << std::endl;
    
    return 0;
}