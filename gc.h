#ifndef HEADER_FILE_H
#define HEADER_FILE_H

#include <functional>

using namespace std;

class gc_object
{
public:
    gc_object();

protected:
    virtual void get_ptrs(std::function<void(gc_object *)> callback);
};

template <typename T> 
class gc_root_ptr;

class gc_root_ptr_base{
    template<typename> friend class gc_root_ptr;
    friend class gc;

    gc_root_ptr_base * prev = nullptr;
    gc_root_ptr_base * next = nullptr;
};


template <typename T> 
class gc_root_ptr : gc_root_ptr_base
{
private:
    T * pt;
public:
    gc_root_ptr(T * p);
    ~gc_root_ptr();
    T *operator->() const;
    T &operator*() const;
    
    T *get() const;
    void reset(T* ptr = nullptr);
    explicit operator bool() const;
};


class gc
{
public:
    static void collect();
};

#endif

// g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o main && ./main