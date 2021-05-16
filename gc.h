#ifndef HEADER_FILE_H
#define HEADER_FILE_H

#include <functional>
#include <iostream>
#include <list>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

#define DEBUG 0

class gc_object_base
{
    friend class gc_object;
    friend class gc;
    bool reachability_flag = false;
    gc_object_base *prev = nullptr;
    gc_object_base *next = nullptr;

public:
    virtual ~gc_object_base()
    {
        if (DEBUG)
            std::cout << "virtual!" << std::endl;
    }
};

gc_object_base head_obj;
gc_object_base *actual_obj = &head_obj;
class gc_object : gc_object_base
{
    friend class gc;

public:
    gc_object()
    {
        if (DEBUG)
            std::cout << "normal constructor" << std::endl;
        prev = actual_obj;
        actual_obj->next = this;
        actual_obj = this;
    }
    gc_object(const gc_object &)
    {
        if (DEBUG)
            std::cout << "copy constructor" << std::endl;
        prev = actual_obj;
        actual_obj->next = this;
        actual_obj = this;
        // we want to rewrite list pointers, because here we are creating a new instance
    }
    gc_object(gc_object &&) = delete;
    gc_object &operator=(const gc_object &)
    {
        if (DEBUG)
            std::cout << "copy assignment" << std::endl;

        return *this;
        // we don't want to rewrite list pointers (next, prev), because that would lead to memory leak
        // we are not creating new instance, just rewriting the old one
    }
    gc_object &operator=(const gc_object &&) = delete;
    ~gc_object()
    {
        if (DEBUG)
            std::cout << "let's call it done chaps!" << std::endl;
        if (actual_obj == this)
        {
            actual_obj = prev;
            //      std::cout << actual_obj << '\t'<< prev << std::endl;
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

protected:
    virtual void get_ptrs(std::function<void(gc_object *)>)
    {
        if (DEBUG)
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

    gc_object *gc_object_pointer = nullptr;
    gc_root_ptr_base *prev = nullptr;
    gc_root_ptr_base *next = nullptr;
};

gc_root_ptr_base head_root;
gc_root_ptr_base *actual_root;
class gc
{
private:
    template <typename T>
    friend class gc_root_ptr;
    static std::condition_variable condition;
    static std::condition_variable myConditionalVariable;
    static std::queue<gc_object *> Queue;
    
    static std::mutex shutdown_mutex;
    static std::mutex add_job_mutex;
    static std::mutex threadpool_mutex;
    static std::mutex wait_mutex;

    static std::vector<std::thread> Pool;
    static int Num_Threads;
    static bool terminate_pool;
    static bool stopped;

    static std::atomic<int> thread_finish_counter;
    static std::atomic<int> job_counter;
    static std::atomic<int> fork_counter;

    static void callback(gc_object *object)
    {
        if (!object)
            return;
        // neco jako if NUmthreads/2 < fork
        // we don't need any complicated synchonization here (it doesn't really matter if we spawn another job even if race condition occures)
        if(fork_counter > 0) {
            fork_counter -= 1;
            Add_Job(object);
            return;
        }
        object->reachability_flag = true;
        object->get_ptrs(callback);
    }
    static void Infinite_loop_function()
    {
        while (true)
        {
            gc_object *Job = nullptr;

            //std::cout << thread_finish_counter << std::endl;
            {
                std::unique_lock<std::mutex> lock(threadpool_mutex);

                condition.wait(lock, [&]()
                               { return !Queue.empty() || terminate_pool; });
                if (!Queue.empty())
                {
                    Job = Queue.front();
                    Queue.pop();
                }
            }
            if (Job != nullptr)
            {
                Job->get_ptrs(callback); // function<void()> type
                fork_counter += 1;
                thread_finish_counter += 1;
                if (thread_finish_counter == job_counter)
                    myConditionalVariable.notify_all();
            }
            else
                break;
        }
    };
    static void Add_Job(gc_object *New_Job)
    {
        {
            std::unique_lock<std::mutex> lock(add_job_mutex);
            Queue.push(New_Job);
        }
        condition.notify_one();
    }
    static void shutdown()
    {
        {
            std::unique_lock<std::mutex> lock(shutdown_mutex);
            terminate_pool = true;
        } // use this flag in condition.wait
        //std::cout << "shutdown" << std::endl;
        condition.notify_all(); // wake up all threads.

        // Join all threads.
        for (std::thread &every_thread : Pool)
        {
            every_thread.join();
        }

        Pool.clear();
        stopped = true; // use this flag in destructor, if not set, call shutdown()
    }

public:
    gc() {}
    static void start_threadpool()
    {
        Num_Threads = std::thread::hardware_concurrency();
        for (int ii = 0; ii < Num_Threads; ii++)
        {
            Pool.push_back(std::thread(Infinite_loop_function));
        }
        terminate_pool = false;
        stopped = false;
    }
    static void collect()
    {
        if (stopped)
        {
            start_threadpool();
        }
        auto mark_iterator = head_root.next;

        while (mark_iterator)
        {
            if (mark_iterator->gc_object_pointer)
            {
                mark_iterator->gc_object_pointer->reachability_flag = true;
                Add_Job(mark_iterator->gc_object_pointer);
                job_counter++;
                //auto f = std::async(std::launch::async, &gc_object::get_ptrs, mark_iterator->gc_object_pointer, f_callback);
            }
            mark_iterator = mark_iterator->next;
        }
        {
            //lock the mutex first!
            std::unique_lock<std::mutex> myLock(wait_mutex);

            //wait till a condition is met
            myConditionalVariable.wait(myLock, [&]()
                                       { return thread_finish_counter == job_counter; });

            thread_finish_counter = 0;
            job_counter = 0;
            fork_counter = 0;
        }
        // sweeping
        gc_object_base *sweep_iterator = head_obj.next;
        //        std::cout << "sweep address: " <<&head_obj<<std::endl;

        while (sweep_iterator)
        {
            //std::cout << "sweep address: " <<sweep_iterator<<std::endl;
            if (DEBUG)
                std::cout << "Sweep it boys" << std::endl;
            if (!(sweep_iterator->reachability_flag))
            {
                gc_object_base *old_it = sweep_iterator;
                sweep_iterator = sweep_iterator->next;
                if (DEBUG)
                    std::cout << "Delete!" << std::endl;
                delete old_it;

                if (DEBUG)
                    std::cout << "No segFault?" << std::endl;
            }
            else
            {
                sweep_iterator->reachability_flag = false;
                sweep_iterator = sweep_iterator->next;
            }
        }
        if (actual_obj == &head_obj)
            shutdown();
    }
};
template <typename T>
class gc_root_ptr : gc_root_ptr_base
{
private:
    T *pt = nullptr;

public:
    gc_root_ptr()
    {
        if (!actual_root)
        {
            actual_root = &head_root;
        }
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
        // we want to rewrite list pointers, because here we are creating a new instance
    }
    gc_root_ptr(gc_root_ptr &&other)
    {
        if (!actual_root)
        {
            actual_root = &head_root;
        }
        //std::cout << "other pt" << other.pt << std::endl;

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
        // we don't want to rewrite list pointers (next, prev), because that would lead to memory leak
        // we are not creating new instance, just rewriting the old one
    }
    gc_root_ptr &operator=(gc_root_ptr &&other)
    {
        pt = other.pt;
        //std::cout << "other pt" << pt << std::endl;
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
            gc::shutdown();
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
            std::cout << "NULL!!!" << std::endl;
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


std::condition_variable gc::condition;
std::condition_variable gc::myConditionalVariable;

std::mutex gc::shutdown_mutex;
std::mutex gc::add_job_mutex;
std::mutex gc::wait_mutex;
std::mutex gc::threadpool_mutex;

std::queue<gc_object *> gc::Queue;
std::vector<std::thread> gc::Pool;

bool gc::terminate_pool = false;
bool gc::stopped = true;

int gc::Num_Threads;
std::atomic<int> gc::thread_finish_counter = 0;
std::atomic<int> gc::job_counter = 0;
std::atomic<int> gc::fork_counter = 0;

#endif

// g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o main && ./main