//#pragma once
#ifndef _DISPATCHQUEUE_H_
#define _DISPATCHQUEUE_H_
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

class DispatchQueue
{
public:
    using  EventFunc = std::function<void()>;
    using  time_point = std::chrono::steady_clock::time_point;

    struct EventEntry
    {
        EventFunc event_handler_;

        uint64_t id_ = 0;
        std::chrono::milliseconds timeout_;
        time_point next_run_;
        bool repeat_ = false;
        bool from_timer_ = false;
        EventEntry() {}
        EventEntry(EventFunc event_handler)
        {
            event_handler_ = std::move(event_handler);
        }
        EventEntry(uint64_t id,
            std::chrono::milliseconds timeout,
            time_point next_run,
            bool repeat,
            EventFunc event_handler) :
            id_(id),
            timeout_(timeout),
            next_run_(next_run),
            repeat_(repeat),
            event_handler_(event_handler),
            from_timer_(true)
        {
        }

        EventEntry(const EventEntry& o)
        {
            this->id_ = o.id_;
            this->timeout_ = o.timeout_;
            this->next_run_ = o.next_run_;
            this->repeat_ = o.repeat_;
            this->event_handler_ = o.event_handler_;
            this->from_timer_ = o.from_timer_;
        }
        EventEntry(EventEntry&& o) noexcept
        {
            this->id_ = o.id_;
            this->timeout_ = o.timeout_;
            this->next_run_ = o.next_run_;
            this->repeat_ = o.repeat_;
            this->event_handler_ = std::move(o.event_handler_);
            o.event_handler_ = nullptr;
            this->from_timer_ = o.from_timer_;
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
                this->from_timer_ = o.from_timer_;
            }
            return *this;
        }
        EventEntry& operator=(EventEntry&& o) noexcept
        {
            if (this != &o)
            {
                this->id_ = o.id_;
                this->timeout_ = o.timeout_;
                this->next_run_ = o.next_run_;
                this->repeat_ = o.repeat_;
                this->event_handler_ = std::move(o.event_handler_);
                o.event_handler_ = nullptr;
                this->from_timer_ = o.from_timer_;
            }

            return *this;
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
        work_concurrentqueue_.enqueue(EventEntry(std::forward<Fn>(func)));
    }
    template<class F, class... Args>
    void DispatchAsync(F&& f, Args&&... args)
    {
        auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        std::function<void()> task = func;
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
            work_concurrentqueue_.enqueue(EventEntry([&]()
                {
                    std::forward<Fn>(func)();

                    std::unique_lock<std::mutex> sync_cb_lock(sync_mtx);
                    completed = true;
                    sync_cond.notify_one();

                }));
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
        return (timer_thread_started_ && work_queue_thread_started && !quit_);
    }
    void Join()
    {
        if (timer_thread_.joinable())
            timer_thread_.join();

        if (work_queue_thread_.joinable())
            work_queue_thread_.join();
    }
    void Stop()
    {
        quit_ = true;
        work_queue_thread_started = false;
        timer_thread_started_ = false;
    }
private:
    void DispatchThreadProc();
    void TimerThreadProc();
    bool InitThread()
    {
        try
        {
            std::thread t(&DispatchQueue::DispatchThreadProc, this);
            work_queue_thread_ = std::move(t);

            std::thread t1(&DispatchQueue::TimerThreadProc, this);
            timer_thread_ = std::move(t1);


            std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);
            timer_cond_.wait(timer_lock, [this] { return timer_thread_started_.load(); });
        }
        catch (const std::exception&)
        {
            return false;
        }

        return true;
    };
private:

    std::thread  work_queue_thread_;
    std::thread  timer_thread_;

    std::atomic<bool> work_queue_thread_started;
    std::atomic<bool> timer_thread_started_;
    std::atomic<bool> quit_;

    moodycamel::ConcurrentQueue<EventEntry> work_concurrentqueue_;

    using  TimerSet = boost::multi_index_container<
        EventEntry,
        boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
        boost::multi_index::member<EventEntry, uint64_t, &EventEntry::id_>>,
        boost::multi_index::ordered_non_unique<
        boost::multi_index::member<EventEntry, time_point, &EventEntry::next_run_>>>>;

    enum
    {
        BY_ID = 0,
        BY_EXPIRATION = 1,
    };

    std::mutex timer_mtx_;
    std::condition_variable timer_cond_;
    TimerSet timers_set_;

    std::atomic<uint64_t> generate_timer_id_;
    std::atomic<bool> fall_through_;
};

#endif
