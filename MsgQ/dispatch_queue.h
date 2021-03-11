//#pragma once
#ifndef _DISPATCHQUEUE_H_
#define _DISPATCHQUEUE_H_
#include <iostream>
#include <queue>
#include <set>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include "concurrentqueue.h"
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/diagnostic_information.hpp>

class DispatchQueue
{
public:
    using  EventFunc = std::function<void()>;

    struct EventEntry
    {
        EventFunc event_handler_;
        uint64_t id_ = 0;
        std::chrono::milliseconds timeout_;
        std::chrono::steady_clock::time_point next_run_;
        bool repeat_ = false;

        EventEntry() {}

        EventEntry(uint64_t id,
            std::chrono::milliseconds timeout,
            std::chrono::steady_clock::time_point next_run,
            bool repeat,
            EventFunc event_handler) :
            id_(id),
            timeout_(timeout),
            next_run_(next_run),
            repeat_(repeat),
            event_handler_(event_handler)
        {
        }

        EventEntry(const EventEntry& o)
        {
            this->id_ = o.id_;
            this->timeout_ = o.timeout_;
            this->next_run_ = o.next_run_;
            this->repeat_ = o.repeat_;
            this->event_handler_ = o.event_handler_;
        }

        EventEntry(EventEntry&& o)
        {
            this->id_ = o.id_;
            this->timeout_ = o.timeout_;
            this->next_run_ = o.next_run_;
            this->repeat_ = o.repeat_;
            this->event_handler_ = std::move(o.event_handler_);
            o.event_handler_ = nullptr;
        }
        EventEntry& operator=(const EventEntry& o)
        {
            if (this != &o)
            {
                this->id_ = o.id_;
                this->timeout_ = o.timeout_;
                this->next_run_ = o.next_run_;
                this->repeat_ = o.repeat_;
                this->event_handler_ = o.event_handler_;
            }
            return *this;
        }
        EventEntry& operator=(EventEntry&& o)
        {
            if (this != &o)
            {
                this->id_ = o.id_;
                this->timeout_ = o.timeout_;
                this->next_run_ = o.next_run_;
                this->repeat_ = o.repeat_;
                this->event_handler_ = std::move(o.event_handler_);
                o.event_handler_ = nullptr;
            }

            return *this;
        }

        operator EventFunc()
        {
            return event_handler_;
        }
    };

    static DispatchQueue& GetDefaultDispatchQueue()
    {
        static DispatchQueue s_dispatchqueue;
        return s_dispatchqueue;
    }
public:
    DispatchQueue();
    ~DispatchQueue();

    DispatchQueue(const DispatchQueue&) = delete;
    DispatchQueue(DispatchQueue&&) = delete;
    DispatchQueue& operator=(const DispatchQueue&) = delete;
    DispatchQueue& operator=(DispatchQueue&&) = delete;

    template<typename Fn>
    void DispatchAsync(Fn&& func)
    {
        work_concurrentqueue_.enqueue(std::forward<Fn>(func));
    }
    template<class F, class... Args>
    void DispatchAsync(F&& f, Args&&... args)
    {
        std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        DispatchAsync(std::move(task));
    }

    template<typename Fn>
    void DispatchSync(Fn&& func)
    {
        std::mutex sync_mtx;
        std::unique_lock< decltype(sync_mtx) > sync_lock(sync_mtx);
        std::condition_variable sync_cond;
        std::atomic< bool > completed(false);

        {
            work_concurrentqueue_.enqueue([&]()
                {
                    std::forward<Fn>(func)();

                    std::unique_lock<std::mutex> sync_cb_lock(sync_mtx);
                    completed = true;
                    sync_cond.notify_one();

                });
        }

        sync_cond.wait(sync_lock, [&] { return completed.load(); });
    }

    template<class F, class... Args>
    void DispatchSync(F&& f, Args&&... args)
    {
        std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

        DispatchSync(std::move(task));
    }


    template<class F, class... Args>
    auto Dispatch(F&& f, Args&&... args)  -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

        std::future<return_type> res = task->get_future();

        DispatchAsync([task]() { (*task)(); });

        return res;
    }

    template<class Rep, class Period, class Fn>
    uint64_t SetTimer(std::chrono::duration<Rep, Period>  timeout_duration, Fn&& fun, bool repeat = true)
    {
        if (!IsRunning())
            return 0;

        EventEntry event_entry(++generate_timer_id_,
            timeout_duration,
            std::chrono::steady_clock::now() + timeout_duration,
            repeat,
            std::forward<Fn>(fun));

        std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);
        auto& by_expiration = timers_set_.get<BY_EXPIRATION>();
        if (!by_expiration.empty() && event_entry.next_run_ < by_expiration.begin()->next_run_)
        {
            fall_through_ = true;
        }
        by_expiration.insert(std::move(event_entry));
        timer_cond_.notify_one();

        return generate_timer_id_;
    }

    template<class Rep, class Period, class F, class... Args>
    uint64_t SetTimer(std::chrono::duration<Rep, Period>  timeout_duration, bool repeat, F&& f, Args&&... args)
    {
        std::function<void()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

        return SetTimer(timeout_duration, std::move(func), repeat);
    }
    bool CancelTimer(uint64_t timer_id);
    bool IsRunning()
    {
        return (start_ && !quit_);
    }
    void Join()
    {
        if (timer_thread_ && timer_thread_->joinable())
            timer_thread_->join();
        timer_thread_ = nullptr;

        if (work_queue_thread_ && work_queue_thread_->joinable())
            work_queue_thread_->join();
        work_queue_thread_ = nullptr;
    }

    bool Start()
    {
        try
        {
            if (start_)
                return false;

            start_ = true;
            quit_ = false;
            work_queue_thread_ = std::make_unique<std::thread>(&DispatchQueue::DispatchThreadProc, this);
            timer_thread_ = std::make_unique<std::thread>(&DispatchQueue::TimerThreadProc, this);
        }
        catch (const std::exception&)
        {
            start_ = false;
            quit_ = true;
            std::cout << boost::current_exception_diagnostic_information() << "\n";
            return false;
        }

        return true;
    };

    void Stop(bool wait = true)
    {
        start_ = false;
        quit_ = true;
        if (wait)
        {
            Join();
        }
    }

private:
    void DispatchThreadProc();
    void TimerThreadProc();

private:

    std::unique_ptr<std::thread>  work_queue_thread_;
    std::unique_ptr<std::thread>  timer_thread_;

    std::atomic<bool> start_ = false;
    std::atomic<bool> quit_ = true;

    moodycamel::ConcurrentQueue<EventFunc> work_concurrentqueue_;

    using  TimerSet = boost::multi_index_container<
        EventEntry,
        boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
        boost::multi_index::member<EventEntry, uint64_t, &EventEntry::id_>>,
        boost::multi_index::ordered_non_unique<
        boost::multi_index::member<EventEntry, std::chrono::steady_clock::time_point, &EventEntry::next_run_>>>>;

    enum
    {
        BY_ID = 0,
        BY_EXPIRATION = 1,
    };

    std::mutex timer_mtx_;
    std::condition_variable timer_cond_;
    TimerSet timers_set_;

    std::atomic<uint64_t> generate_timer_id_;
    bool fall_through_;
};

#endif
