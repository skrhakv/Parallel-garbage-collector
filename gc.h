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
    virtual ~gc_object_base() {}
};

// head & tail for gc_object
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
    }
    gc_object(gc_object &&) = delete; // move constructor is actually never called (copy constructor is called instead)
    gc_object &operator=(const gc_object &)
    {
        if (DEBUG)
            std::cout << "copy assignment" << std::endl;

        return *this;
    }
    gc_object &operator=(const gc_object &&) = delete; // move assignment is actually never called (copy constructor is called instead)
    ~gc_object()
    {
        if (DEBUG)
            std::cout << "let's call it done chaps! (~gc_object)" << std::endl;
        if (actual_obj == this)
            actual_obj = prev;

        // if prev/next are not null then keep the list valid
        if (prev)
            prev->next = next;

        if (next)
            next->prev = prev;
    }

protected:
    virtual void get_ptrs(std::function<void(gc_object *)>) {}
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


// head & tail of gc_root_ptr list
gc_root_ptr_base head_root;
gc_root_ptr_base *actual_root;
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

    static void callback(gc_object *object)
    {
        if (!object)
            return;
        // we don't need any complicated synchonization here (it doesn't really matter if we spawn another job even if race condition occures)
        // std::cout << fork_counter << std::endl;

        object->reachability_flag = true;
        if (fork_counter > 0)
        {
            fork_counter -= 1;
            job_counter += 1;
            add_job(object);
            return;
        }
        object->get_ptrs(callback);
    }
    static void threadpool_loop()
    {
        while (true)
        {
            gc_object *job = nullptr;

            {
                std::unique_lock<std::mutex> lock(threadpool_mutex);

                threadpool_condition.wait(lock, [&]()
                                          { return !queue.empty() || terminate_pool; });
                if (!queue.empty())
                {
                    job = queue.front();
                    queue.pop();
                }
            }
            if (job != nullptr)
            {
                job->get_ptrs(callback); 
                fork_counter += 1;
                //std::cout << "increase fork_counter: "<< fork_counter << std::endl;
                thread_finish_counter += 1;
                //std:: cout << thread_finish_counter << '\t' << job_counter << std::endl;

                if (thread_finish_counter == job_counter)
                    end_of_marking_condition.notify_all();
            }
            else
                break;
        }
    };
    static void add_job(gc_object *New_Job)
    {
        {
            std::unique_lock<std::mutex> lock(add_job_mutex);
            queue.push(New_Job);
        }
        threadpool_condition.notify_one();
    }
    static void terminate_threads()
    {
        {
            std::unique_lock<std::mutex> lock(shutdown_mutex);
            terminate_pool = true;
        }

        threadpool_condition.notify_all();
        for (std::thread &every_thread : pool)
        {
            every_thread.join();
        }

        pool.clear();
        stopped = true; 
    }

public:
    gc() {}
    static void start_threadpool()
    {
        hw_threads = std::thread::hardware_concurrency();
        for (int i = 0; i < hw_threads; i++)
        {
            pool.push_back(std::thread(threadpool_loop));
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
        fork_counter = hw_threads;
        thread_finish_counter = 0;
        job_counter = 0;
        while (mark_iterator)
        {
            if (mark_iterator->gc_object_pointer)
            {
                mark_iterator->gc_object_pointer->reachability_flag = true;

                // check if we are at the end gc_root_ptr list
                //if(mark_iterator->next)
                {
                    add_job(mark_iterator->gc_object_pointer);
                    job_counter++;
                }
               
            }
            mark_iterator = mark_iterator->next;
        }
        
        {
            std::unique_lock<std::mutex> myLock(wait_mutex);

            end_of_marking_condition.wait(myLock, [&]()
                                          { return thread_finish_counter == job_counter; });
        }
        // sweeping
        gc_object_base *sweep_iterator = head_obj.next;
        //std::cout << "here1" << std::endl;

        while (sweep_iterator)
        {
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
            terminate_threads();
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

std::condition_variable gc::threadpool_condition;
std::condition_variable gc::end_of_marking_condition;

std::mutex gc::shutdown_mutex;
std::mutex gc::add_job_mutex;
std::mutex gc::wait_mutex;
std::mutex gc::threadpool_mutex;

std::queue<gc_object *> gc::queue;
std::vector<std::thread> gc::pool;

bool gc::terminate_pool = false;
bool gc::stopped = true;
int gc::hw_threads;

std::atomic<int> gc::thread_finish_counter = 0;
std::atomic<int> gc::job_counter = 0;
std::atomic<int> gc::fork_counter = 0;

#endif

// g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o main && ./main