#include <cassert>
#include <iostream>
#include <functional>
#include <string>
#include "gc.h"
#include <string>

class Node : public gc_object
{
public:
    int val;
    Node *left{nullptr};
    Node *right{nullptr};
    // Node(Node&a) : gc_object(a){}
    // Node(Node&&a) : gc_object(std::move(a)){}
    // Node& operator=(const Node&&) // III. copy assignment
    // {
    //     std::cout<< "move assignment" << std::endl;
    //     return *this;
    //     // we don't want to rewrite list pointers (next, prev), because that would lead to memory leak
    //     // we are not creating new instance, just rewriting the old one
    // }Node& operator=(const Node&) // III. copy assignment
    // {
    //     std::cout<< "move assignment" << std::endl;
    //     return *this;
    //     // we don't want to rewrite list pointers (next, prev), because that would lead to memory leak
    //     // we are not creating new instance, just rewriting the old one
    // }
    //
    Node(int val) : val(val) {}

    ~Node()
    {
        std::cout << "Deleted:" << val << std::endl;
    }

protected:
    void get_ptrs(std::function<void(gc_object *)> callback) override
    {
        callback(left);
        callback(right);
    }
};

class BinaryTree
{
public:
    void add(int val);
    void addBalancedRange(int from, int to);
    void detachSubtree(int val);

private:
    gc_root_ptr<Node> root_;
};

void BinaryTree::add(int val)
{
    if (!root_)
    {
        root_ = new Node(val);
        return;
    }

    Node *p = root_.get();
    while (true)
    {
        if (val == p->val)
        {
            return;
        }
        else if (val < p->val)
        {
            if (!p->left)
            {
                p->left = new Node(val);
                return;
            }
            else
            {
                p = p->left;
            }
        }
        else
        { // val > p->val
            if (!p->right)
            {
                p->right = new Node(val);
                return;
            }
            else
            {
                p = p->right;
            }
        }
    }
}

void BinaryTree::addBalancedRange(int from, int to)
{
    if (from == to)
    {
        add(from);
    }
    else
    {
        int middle = (from + to) / 2;
        add(middle);

        if (from != middle)
        {
            addBalancedRange(from, middle - 1);
        }

        if (to != middle)
        {
            addBalancedRange(middle + 1, to);
        }
    }
}

void BinaryTree::detachSubtree(int val)
{
    if (!root_)
    {
        return;
    }

    Node *p = root_.get();
    if (val == p->val)
    {
        root_.reset();
        return;
    }

    while (true)
    {
        assert(val != p->val);

        if (val < p->val)
        {
            if (!p->left)
            {
                return;
            }
            else if (val == p->left->val)
            {
                p->left = nullptr;
                return;
            }
            else
            {
                p = p->left;
            }
        }
        else
        { // val > p->val
            if (!p->right)
            {
                return;
            }
            else if (val == p->right->val)
            {
                p->right = nullptr;
                return;
            }
            else
            {
                p = p->right;
            }
        }
    }
}

// Basic usage
void test1()
{
    {
        gc_root_ptr<Node> treeRoot = new Node(42);
        treeRoot->left = new Node(41);
        treeRoot->right = new Node(43);

        treeRoot->right = new Node(44);
        gc::collect(); // "Deleted: 43"
        std::cout << std::endl;
    }
    
    gc::collect(); // "Deleted: 42", "Deleted: 41", "Deleted: 44"
    std::cout << std::endl;
    std::cout << "KONEcek?" << std::endl;
}

// gc_object interface
void test2()
{
    gc_root_ptr<Node> r = new Node(1);

    Node *n = new Node(*r); // protoze Node nebude mit zadneho gc_root_ptr predka, ktery by na nej mel odkaz?
    r->val = 2;
    gc::collect(); // "Deleted: 1"

    n = new Node(3);
    *n = *r;
    r->val = 4;
    gc::collect(); // "Deleted: 2"
    n = new Node(std::move(*r));
    r.reset();
    gc::collect(); // "Deleted: 4", "Deleted: 4"

    n = new Node(5);
    Node *n2 = new Node(6);
    *n2 = std::move(*n);
    gc::collect(); // "Deleted: 5", "Deleted: 5"
}

// gc_root_ptr<T> interface
void test3()
{
    gc_root_ptr<Node> r;

    r = new Node(1);
    std::cout << r.get()->val << std::endl;
    std::cout << (*r).val << std::endl;
    std::cout << r->val << std::endl;
    std::cout << (r.get() != nullptr ? "OK" : "KO") << std::endl;
    std::cout << (r ? "OK" : "KO") << std::endl;

    r.reset(new Node(2));
    std::cout << r->val << std::endl;

    r.reset();
    std::cout << (r.get() == nullptr ? "OK" : "KO") << std::endl;
    std::cout << (!r ? "OK" : "KO") << std::endl;
    std::cout << (r ? "KO" : "OK") << std::endl;

    gc::collect(); // "Deleted: 1", "Deleted: 2"

    r = new Node(3);
    gc_root_ptr<Node> r2(r);
    r.reset();
    gc::collect(); // Nothing

    {
        gc_root_ptr<Node> rt1 = new Node(4);
        r = std::move(rt1);
        r.reset();
        gc::collect(); // "Deleted: 4"

        gc_root_ptr<Node> rt2 = new Node(5);
        gc_root_ptr<Node> rt3(std::move(rt2));
        rt3.reset();
        gc::collect(); // "Deleted: 5"
    }
}

// gc_object and gc_root_ptr<T> global list management
void test4()
{
    Node *n = new Node(90);
    gc::collect(); // "Deleted: 90"

    n = new Node(42);
    n->right = new Node(24);
    n->right->left = n;
    gc::collect(); // "Deleted: 42", "Deleted: 24"

    gc_root_ptr<Node> r;
    {
        gc_root_ptr<Node> r1 = new Node(1);
        gc_root_ptr<Node> r2 = new Node(2);
        gc_root_ptr<Node> r3 = new Node(3);
        gc_root_ptr<Node> r4 = new Node(4);

        r2.reset();
        gc::collect(); // "Deleted: 2"

        r1.reset();
        gc::collect(); // "Deleted: 1"

        r4.reset();
        gc::collect(); // "Deleted: 4"

        r3->right = new Node(11);
        r3->right->right = new Node(12);
        r3->right->right->right = new Node(13);
        r3->right->right->right->right = new Node(14);
        gc::collect(); // Nothing
        std::cout << "After GC" << std::endl;

        r3 = new Node(42);
        gc::collect(); // "Deleted: 3", "Deleted: 11", "Deleted: 12", "Deleted: 13", "Deleted: 14"

        r = std::move(r3);
        gc::collect(); // Nothing
        std::cout << "After GC" << std::endl;
    }

    gc::collect(); // Nothing
    std::cout << "After GC" << std::endl;

    r.reset();
    gc::collect(); // "Deleted: 42"
}

// Complex usage
void test5()
{
    {
        BinaryTree tree;
        tree.addBalancedRange(1, 15);

        tree.detachSubtree(3);
        gc::collect();
        std::cout << std::endl;

        tree.detachSubtree(6);
        gc::collect();
        std::cout << std::endl;

        tree.detachSubtree(12);
        gc::collect();
        std::cout << std::endl;

        tree.detachSubtree(2);
    }
    gc::collect();
    std::cout << std::endl;
}

// Performance
void test6()
{
    const size_t maxElements = (1 << 23) - 1;

    BinaryTree tree;
    tree.addBalancedRange(1, maxElements);

    tree.detachSubtree(1);
    gc::collect();
    std::cout << std::endl;

    tree.detachSubtree(maxElements / 2);
    gc::collect();
    std::cout << std::endl;

    tree.detachSubtree(maxElements / 2 + 2);
    gc::collect();
    std::cout << std::endl;

    tree.detachSubtree(maxElements - 1);
    gc::collect();
    std::cout << std::endl;

    tree.detachSubtree(6);
    gc::collect();
    std::cout << std::endl;

    tree.detachSubtree(maxElements - 5);
    gc::collect();
    std::cout << std::endl;

    tree.detachSubtree(8);
    gc::collect();
    std::cout << std::endl;

    tree.detachSubtree(maxElements - 8);
    gc::collect();
    std::cout << std::endl;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cout << "Provide the test number" << std::endl;
        return 1;
    }

    int testNo = std::stoi(argv[1]);
    switch (testNo)
    {
    case 1:
        test1();
        break;

    case 2:
        test2();
        break;

    case 3:
        test3();
        break;

    case 4:
        test4();
        break;

    case 5:
        test5();
        break;

    case 6:
        #include <chrono>
        using std::chrono::duration;
        using std::chrono::duration_cast;
        using std::chrono::high_resolution_clock;
        using std::chrono::milliseconds;
        auto t1 = high_resolution_clock::now();
        test6();
        auto t2 = high_resolution_clock::now();
        auto ms_int = duration_cast<milliseconds>(t2 - t1);
        std::cout << ms_int.count() << "ms\n";
        break;
    }

    return 0;
}
