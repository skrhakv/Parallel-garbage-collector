#ifndef HEADER_FILE_H
#define HEADER_FILE_H

#include <functional>
#include <iostream>
#include <list>

#define DEBUG 0

class gc_object_base{
    friend class gc_object;
    friend class gc;
    bool reachability_flag = false;
    gc_object_base *prev = nullptr;
    gc_object_base *next = nullptr;
public:
    virtual ~gc_object_base(){
        if(DEBUG)
            std::cout << "virtual!" << std::endl;
    }
};

gc_object_base head_obj;
gc_object_base * actual_obj = &head_obj;
class gc_object: gc_object_base
{
    friend class gc;
public:
    gc_object()
    {
         if(DEBUG)
            std::cout << "normal constructor" << std::endl;
        prev = actual_obj;
        actual_obj->next = this;
        actual_obj = this; 
    }
    gc_object(const gc_object&) 
    {
        if(DEBUG)
            std::cout << "copy constructor" << std::endl;
        prev = actual_obj;
        actual_obj->next = this;
        actual_obj = this;
        // we want to rewrite list pointers, because here we are creating a new instance
    }
    gc_object( gc_object&&) = delete;
    gc_object& operator=(const gc_object&) 
    {
        if(DEBUG)
            std::cout<< "copy assignment" << std::endl; 

        return *this;
        // we don't want to rewrite list pointers (next, prev), because that would lead to memory leak
        // we are not creating new instance, just rewriting the old one
    }
    gc_object& operator=(const gc_object&&) = delete;
    ~gc_object(){
        if(DEBUG)
            std::cout << "let's call it done chaps!" << std::endl;
        if(actual_obj == this){
            actual_obj = prev;
        }
        if(prev) {
            prev->next = next;
        }
        if(next) {
            next->prev = prev;
        }
    }

protected:
    virtual void get_ptrs(std::function<void(gc_object *)> )
    {
        if(DEBUG)
            std::cout << "get pointers in base class" << std::endl;
    }
};

template <typename T>
class gc_root_ptr;

class gc_root_ptr_base
{
    template <typename>
    friend class gc_root_ptr;
    friend class gc;
    
    gc_object * gc_object_pointer = nullptr;
    gc_root_ptr_base *prev = nullptr;
    gc_root_ptr_base *next = nullptr;
};

gc_root_ptr_base head_root;
gc_root_ptr_base *actual_root;

template <typename T>
class gc_root_ptr : gc_root_ptr_base
{
private:
    T *pt = nullptr;

public:
    gc_root_ptr(){
        if (!actual_root)
        {
            actual_root = &head_root;
        }
        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
    }
    gc_root_ptr(const gc_root_ptr&other) 
    {  
        if (!actual_root)
        {
            actual_root = &head_root;
        }
        if(DEBUG)
            std::cout << "copy constructor" << std::endl;
        pt = other.pt;
        gc_object_pointer = other.gc_object_pointer;
        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
        // we want to rewrite list pointers, because here we are creating a new instance
    }
    gc_root_ptr( gc_root_ptr&& other)  {
        if (!actual_root)
        {
            actual_root = &head_root;
        }
        pt = std::move(other.pt);
        gc_object_pointer = std::move(other.gc_object_pointer);
        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
        other.pt = nullptr;
        other.gc_object_pointer = nullptr;
    }
    gc_root_ptr& operator=(const gc_root_ptr& other) 
    {
        if(DEBUG)
            std::cout<< "copy assignment" << std::endl; 
        pt = other.pt;
        gc_object_pointer = other.gc_object_pointer;
        return *this;
        // we don't want to rewrite list pointers (next, prev), because that would lead to memory leak
        // we are not creating new instance, just rewriting the old one
    }
    gc_root_ptr& operator=(gc_root_ptr&& other) {
        pt = other.pt;
        gc_object_pointer = other.gc_object_pointer;
        other.pt = nullptr;
        other.gc_object_pointer = nullptr;
        return *this;
    }
    gc_root_ptr(T *p)
    {
        static_assert(std::is_base_of<gc_object, T>::value, "T must derive from gc_object!");
        if (!actual_root)
        {
            actual_root = &head_root;
        }
        gc_object_pointer = (gc_object*)p;
        pt = p;
        
        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
        next = nullptr;
    }
    ~gc_root_ptr()
    {
        if(DEBUG)
            std::cout<<"gc_root_ptr Destructor"<<std::endl;

        if (actual_root == this)
        {
            actual_root = prev;
        }
        if (prev)
        {
            prev->next = next;
        }
        if (next)
        {
            next->prev = prev;
        }
    }
    T *operator->() const
    {
        return pt;
    }
    T &operator*() const
    {
        return *pt;
    }

    T *get() const
    {
        return this->pt;
    }
    void reset(T *ptr = nullptr)
    {
        if(!ptr && DEBUG)
            std::cout << "NULL!!!" << std::endl;
        this->pt = ptr;
        this->gc_object_pointer = (gc_object*)ptr;
    }
    explicit operator bool() const
    {
        if (pt)
            return true;
        return false;
    }
};

class gc
{
private:
    static void callback(gc_object* object){
        if(!object)
            return;
        object->reachability_flag = true;
        object->get_ptrs(callback);
    }
public:
    static void collect()
    {
        auto mark_iterator = head_root.next;
        
        
        while (mark_iterator)
        {
            if(mark_iterator->gc_object_pointer) {
                std::function<void(gc_object*)> f_callback = callback;
                mark_iterator->gc_object_pointer->reachability_flag = true;
                mark_iterator->gc_object_pointer->get_ptrs(f_callback);
            }
            mark_iterator = mark_iterator->next;
        }

        // sweeping
        gc_object_base * sweep_iterator = head_obj.next;

        while(sweep_iterator){
            if(DEBUG)
                std::cout << "Sweep it boys" << std::endl;
            if(!(sweep_iterator->reachability_flag)){
                gc_object_base * old_it = sweep_iterator;
                sweep_iterator = sweep_iterator->next;
                if(DEBUG)
                    std::cout << "Delete!" <<std::endl;
                delete old_it;

                if(DEBUG)
                    std::cout << "No segFault?" <<std::endl;
            }
            else{
                sweep_iterator->reachability_flag = false;
                sweep_iterator = sweep_iterator->next;
            }
        }
    }
};

#endif

// g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o main && ./main