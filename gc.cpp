#include <iostream>
#include <list>
#include "gc.h"

gc_object::gc_object()
{
    if (DEBUG)
        std::cout << "normal constructor" << std::endl;
    prev = actual_obj;
    actual_obj->next = this;
    actual_obj = this;
}

gc_object::gc_object(const gc_object &)
{
    if (DEBUG)
        std::cout << "copy constructor" << std::endl;
    prev = actual_obj;
    actual_obj->next = this;
    actual_obj = this;
}

gc_object &gc_object::operator=(const gc_object &)
{
    if (DEBUG)
        std::cout << "copy assignment" << std::endl;

    return *this;
}

gc_object::~gc_object()
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

gc_object_base gc_object_base::head_obj;
gc_object_base *gc_object_base::actual_obj = &head_obj;

gc_root_ptr_base gc_root_ptr_base::head_root;
gc_root_ptr_base *gc_root_ptr_base::actual_root = &head_root;

void gc::callback(gc_object *object)
{
    if (!object)
        return;
    
    object->reachability_flag = true;

    // we don't need any complicated synchonization here (it doesn't really matter if we spawn another job even if race condition occures)
    if (fork_counter > 0)
    {
        fork_counter -= 1;
        job_counter += 1;
        add_job(object);
        return;
    }
    object->get_ptrs(callback);
}
void gc::threadpool_loop()
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

            // notify other threads that one thread has just finished (to spawn new job)
            fork_counter += 1;

            thread_finish_counter += 1;

            // if number of finished jobs equals job counter, then wake up the main thread
            if (thread_finish_counter == job_counter)
                end_of_marking_condition.notify_one();
        }
        else
            break;
    }
}

void gc::add_job(gc_object *New_Job)
{
    {
        std::unique_lock<std::mutex> lock(add_job_mutex);
        queue.push(New_Job);
    }
    threadpool_condition.notify_one();
}

void gc::terminate_threads()
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
void gc::start_threadpool()
{
    hw_threads = std::thread::hardware_concurrency();
    for (int i = 0; i < hw_threads; i++)
    {
        pool.push_back(std::thread(threadpool_loop));
    }
    terminate_pool = false;
    stopped = false;
}

void gc::collect()
{
    if (stopped)
    {
        start_threadpool();
    }
    auto mark_iterator = gc_root_ptr_base::head_root.next;
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
    gc_object_base *sweep_iterator = gc_object_base::head_obj.next;
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
    if (gc_object_base::actual_obj == &(gc_object_base::head_obj))
        terminate_threads();
}
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
