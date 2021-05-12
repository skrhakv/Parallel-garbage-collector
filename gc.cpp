#include <iostream>
#include <list>
#include "gc.h"

gc_object head_gc_obj;
gc_object * actual_gc_obj = &head_gc_obj;

gc_object::gc_object()
{

}

void gc_object::get_ptrs(std::function<void(gc_object *)> callback)
{
    std::cout << "get pointers in base class" << std::endl;
}

gc_root_ptr_base head_root;
gc_root_ptr_base *actual_root = &head_root;

template <typename T>
gc_root_ptr<T>::gc_root_ptr(T *p)
{
    //gc_root_ptr_base* moi = this;
    //root_objs.push_back((gc_root_ptr_base*)this);
    pt = p;
    prev = actual_root;
    actual_root->next = this;
    actual_root = this;
    next = nullptr;
}

template <typename T>
gc_root_ptr<T>::~gc_root_ptr()
{
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
template <typename T>
T *gc_root_ptr<T>::operator->() const { return pt; }

template <typename T>
T &gc_root_ptr<T>::operator*() const { return *pt; }

template <typename T>
T *gc_root_ptr<T>::get() const
{
    return this->pt;
}

template <typename T>
void gc_root_ptr<T>::reset(T *ptr)
{
    this->pt = ptr;
}

template <typename T>
gc_root_ptr<T>::operator bool() const
{
    if (pt)
        return true;
    return false;
}

void gc::collect()
{
    auto iterator = &head_root;
    while (iterator->next)
    {
        iterator = iterator->next;
    }
}
