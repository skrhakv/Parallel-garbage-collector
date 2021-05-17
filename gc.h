#ifndef HEADER_FILE_H
#define HEADER_FILE_H

#include <functional>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

#define DEBUG 0

class gc_object_base
{
private:
    friend class gc_object;
    friend class gc;

    // indicates if object should be deleted when sweeping
    bool reachability_flag = false;

    // prev & next for gc_object's list
    gc_object_base *prev = nullptr;
    gc_object_base *next = nullptr;

    // static head & tail for gc_object's list
    static gc_object_base head_obj;
    static gc_object_base *actual_obj;

public:
    virtual ~gc_object_base() {}
};


class gc_object : gc_object_base
{
    friend class gc;

public:
    gc_object();
    gc_object(const gc_object &);
    gc_object(gc_object &&) = delete; // move constructor is actually never called (copy constructor is called instead)
    gc_object &operator=(const gc_object &);
    gc_object &operator=(const gc_object &&) = delete; // move assignment is actually never called (copy constructor is called instead)
    ~gc_object();

protected:
    virtual void get_ptrs(std::function<void(gc_object *)>) {}
};

class gc_root_ptr_base
{
    template <typename>
    friend class gc_root_ptr;
    friend class gc;

    gc_object *gc_object_pointer = nullptr;

    // prev & next for gc_root_ptr's list
    gc_root_ptr_base *prev = nullptr;
    gc_root_ptr_base *next = nullptr;

    // static head & tail for gc_root_ptr's list
    static gc_root_ptr_base head_root;
    static gc_root_ptr_base *actual_root;
};

class gc
{
private:
    template <typename T>
    friend class gc_root_ptr;

    static std::condition_variable threadpool_condition;
    static std::condition_variable end_of_marking_condition;

    static std::mutex shutdown_mutex;
    static std::mutex add_job_mutex;
    static std::mutex threadpool_mutex;
    static std::mutex wait_mutex;

    static std::vector<std::thread> pool;
    static std::queue<gc_object *> queue;

    static int hw_threads;
    static bool terminate_pool;
    static bool stopped;

    static std::atomic<int> thread_finish_counter;
    static std::atomic<int> job_counter;
    static std::atomic<int> fork_counter;

    static void callback(gc_object *object);
    static void threadpool_loop();
    static void add_job(gc_object *New_Job);
    static void terminate_threads();

public:
    gc() {}
    static void start_threadpool();
    static void collect();
};
template <typename T>
class gc_root_ptr : gc_root_ptr_base
{
private:
    T *pt = nullptr;

public:
    gc_root_ptr()
    {
        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
    }
    gc_root_ptr(const gc_root_ptr &other)
    {
        if (!actual_root)
        {
            actual_root = &head_root;
        }
        if (DEBUG)
            std::cout << "copy constructor" << std::endl;
        pt = other.pt;
        gc_object_pointer = other.gc_object_pointer;
        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
    }
    gc_root_ptr(gc_root_ptr &&other)
    {
        if (!actual_root)
        {
            actual_root = &head_root;
        }

        pt = other.pt;
        gc_object_pointer = other.gc_object_pointer;
        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
        other.pt = nullptr;
        other.gc_object_pointer = nullptr;
    }
    gc_root_ptr &operator=(const gc_root_ptr &other)
    {
        if (DEBUG)
            std::cout << "copy assignment" << std::endl;
        pt = other.pt;
        gc_object_pointer = other.gc_object_pointer;
        return *this;
    }
    gc_root_ptr &operator=(gc_root_ptr &&other)
    {
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
        gc_object_pointer = (gc_object *)p;
        pt = p;

        prev = actual_root;
        actual_root->next = this;
        actual_root = this;
        next = nullptr;
    }
    ~gc_root_ptr()
    {
        if (DEBUG)
            std::cout << "gc_root_ptr Destructor" << std::endl;

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
        if (&head_root == actual_root)
            gc::terminate_threads();
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
        if (!ptr && DEBUG)
            std::cout << "ptr is NULL!" << std::endl;
        this->pt = ptr;
        this->gc_object_pointer = (gc_object *)ptr;
    }
    explicit operator bool() const
    {
        if (pt)
            return true;
        return false;
    }
};

#endif

// g++ -o main -std=c++17  -Wall -Wextra -Wpedantic -pthread gc.cpp recodex_main.cpp && ./main 6