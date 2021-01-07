// From an implementation by Anthony Williams (http://www.justsoftwaresolutions.co.uk/2008/09/)

#ifndef THREAD_SAFE_STACK_H
#define THREAD_SAFE_STACK_H

#include <mutex>
#include <condition_variable>
#include <queue>

template<typename T,
         template <typename T, typename Alloc = std::allocator<T> > class container = std::deque>
class thread_safe_stack
{
private:
    container<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_variable_;
public:
    void push(T const& data)
    {
        {
            std::unique_lock lock(mutex_);
            queue_.push_back(data);
        } //lock.unlock();
        condition_variable_.notify_one();
    }

    bool empty() const
    {
        std::unique_lock lock(mutex_);
        return queue_.empty();
    }

    bool try_pop(T& popped_value)
    {
        std::unique_lock lock(mutex_);
        if(queue_.empty())
            return false;
        popped_value = queue_.back();
        queue_.pop_back();
        return true;
    }

    bool try_top(T& top_value)
    {
        std::unique_lock lock(mutex_);
        if (queue_.empty())
            return false;
        top_value = queue_.back();
        return true;
    }

    /* see next method for a better implementation
    void wait_and_pop(T& popped_value)
    {
        std::unique_lock lock(mutex_);
        while(queue_.empty())
        {
            condition_variable_.wait(lock);
        }

        popped_value = queue_.back();
        queue_.pop_back();
    }
    */

    void wait_and_pop(T& popped_value)
    {
        std::unique_lock lock(mutex_);
        condition_variable_.wait(lock, [this] { return !queue_.empty(); });
        popped_value = queue_.back();
        queue_.pop_back();
    }

    void wait_and_top(T& top_value)
    {
        std::unique_lock lock(mutex_);
        condition_variable_.wait(lock, [this] { return !queue_.empty(); });
        top_value = queue_.back();
    }


    template<typename Duration>
    bool timed_wait_and_pop(T& popped_value, Duration const& wait_duration)
    {
        std::unique_lock lock(mutex_);
        if(!condition_variable_.wait_for(lock, wait_duration, [this] { return !queue_.empty(); }))
            return false;
        popped_value = queue_.back();
        queue_.pop_back();
        return true;
    }
};


#endif //THREAD_SAFE_STACK_H
