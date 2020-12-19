// Da una implementazione di Anthony Williams (http://www.justsoftwaresolutions.co.uk/2008/09/)
// todo: rivedere secondo: https://github.com/anthonywilliams/ccia_code_samples/blob/main/listings/listing_6.2.cpp

// concurrent_queue e' come std::queue un adattatore per contenitori di tipo Sequence
// quindi e' possibile istanziare concurrent_queue con std::deque, std::list ecc.
// Questo adattatore a differenza di std::queue consente operazioni atomiche sulla coda
// quindi la sua interfaccia e' diversa di conseguenza e segue un modello diffuso

#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

template<typename T,
         template <typename T, typename Alloc = std::allocator<T> > class container = std::deque>
class thread_safe_queue
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
        {
            return false;
        }

        popped_value = queue_.front();
        queue_.pop_front();
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

        popped_value = queue_.front();
        queue_.pop_front();
    }
    */
    
    void wait_and_pop(T& popped_value)
    {
        std::unique_lock lock(mutex_);
        condition_variable_.wait(lock, [this] { return !queue_.empty(); });
        popped_value = queue_.front();
        queue_.pop_front();
    }

    template<typename Duration>
    bool timed_wait_and_pop(T& popped_value, Duration const& wait_duration)
    {
        std::unique_lock lock(mutex_);
        if(!condition_variable_.wait_for(lock, wait_duration, [this] { return !queue_.empty(); }))
            return false;
        popped_value = queue_.front();
        queue_.pop_front();
        return true;
    }
};


#endif //THREAD_SAFE_QUEUE_H
