//#pragma once
#ifndef _DISPATCHQUEUE_H_
#define _DISPATCHQUEUE_H_

#include <iostream>
#include <array>
#include <future>

#include "boost/function.hpp"
#include "boost/lockfree/queue.hpp"
#include "boost/thread.hpp"
#include "boost/atomic.hpp"
#include "boost/scoped_ptr.hpp"
#include "boost/chrono.hpp"

#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/exception/exception.hpp>

class DispatchQueue
{
public:
    DispatchQueue() :task_queue_(10240)
    {

    }
    ~DispatchQueue()
    {
        Stop();
    }

    bool Start()
    {
        try
        {
            boost::unique_lock<boost::mutex> lc(mtx_);

            if (start_)
                return false;

            start_ = true;
            quit_ = false;
            task_handle_thread_.reset(new boost::thread(&DispatchQueue::TaskProc, this));
            timer_handle_thread_.reset(new boost::thread(&DispatchQueue::TimerProc, this));
        }
        catch (const std::exception& e)
        {
            std::cout << boost::current_exception_diagnostic_information() << "\n";
            Stop();
            return false;
        }
   
        return true;
    }

    void Stop()
    {
        {
            boost::unique_lock<boost::mutex> lc(mtx_);

            start_ = false;
            quit_ = true;

            cond_var_.notify_one();
        }

        if (task_handle_thread_ && task_handle_thread_->joinable())
        {
            task_handle_thread_->join();
        }

        task_handle_thread_.reset();

        if (timer_handle_thread_ && timer_handle_thread_->joinable())
        {
            timer_handle_thread_->join();
        }

        timer_handle_thread_.reset();

    }

    template<typename F>
    void DispatchAsync(F&& f)
    {
        std::array<char, sizeof(boost::function<void()>)> item;
        new (&item[0]) boost::function<void()>(std::forward<F>(f));
        while (!task_queue_.push(item))continue;
    }

    template<class F, class... Args>
    void DispatchAsync(F&& f, Args&&... args)
    {
        boost::function<void()> task = boost::bind(std::forward<F>(f), std::forward<Args>(args)...);
        DispatchAsync(std::move(task));
    }

    template<class F, class... Args>
    auto Dispatch(F&& f, Args&&... args)  -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared< std::packaged_task<return_type()>>(boost::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();

        DispatchAsync([task]() { (*task)(); });

        return res;
    }

    template<class Rep, class Period, class Fn>
    uint64_t SetTimer(boost::chrono::duration<Rep, Period>  timeout_duration, Fn&& fun, bool repeat = true)
    {
        if (quit_)
            return 0;

        TimeEvent event(++next_id_,
            boost::chrono::steady_clock::now() + timeout_duration,
            (repeat ? boost::chrono::duration_cast<boost::chrono::nanoseconds>(timeout_duration) : boost::chrono::nanoseconds(-1)),
            std::forward<Fn>(fun));

        boost::unique_lock<decltype(mtx_)> lc(mtx_);
        auto& by_expiration = timeouts_.get<BY_EXPIRATION>();
        if (!by_expiration.empty() && event.expiration < by_expiration.begin()->expiration)
        {
            fall_through_ = true;
        }
        by_expiration.insert(std::move(event));


        cond_var_.notify_one();


        return next_id_;
    }

    template<class Rep, class Period, class F, class... Args>
    uint64_t SetTimer(boost::chrono::duration<Rep, Period>  timeout_duration, bool repeat, F&& f, Args&&... args)
    {
        boost::function<void()> func = boost::bind(std::forward<F>(f), std::forward<Args>(args)...);

        return SetTimer(timeout_duration, std::move(func), repeat);
    }

    bool CancelTimer(uint64_t timer_id)
    {
        if (timer_id == 0)
            return false;

        boost::unique_lock<decltype(mtx_)> lc(mtx_);

        auto& by_id = timeouts_.get<BY_ID>();
        auto& by_expiration = timeouts_.get<BY_EXPIRATION>();

        auto it = by_id.find(timer_id);
        if (it != by_id.end())
        {
            if (by_expiration.begin()->id == it->id)
            {
                by_id.erase(it);

                fall_through_ = true;
                cond_var_.notify_one();
                return true;
            }
            else
            {
                by_id.erase(it);
                return true;
            }
        }
        else
        {
            return false;
        }

        return true;
    }

    static DispatchQueue& GetDefaultDispatchQueue()
    {
        static DispatchQueue s_v;
        return s_v;
    }
private:


    void TaskProc()
    {
        std::array<char, sizeof(boost::function<void()>)> item;

        while (!quit_)
        {
            if (task_queue_.pop(item))
            {
                auto p = reinterpret_cast<boost::function<void()>*>(&item[0]);
                try
                {
                    (*p)();
                }
                catch (...)
                {
                    std::cout << "task_queue_ handle exception:" << boost::current_exception_diagnostic_information() << "\n";
                }

                p->~function();
            }
        }

    }

    void TimerProc()
    {
        std::array<char, sizeof(boost::function<void()>)> data;
        boost::unique_lock< decltype(mtx_) > lc(mtx_);

        while (!quit_)
        {
            if (timeouts_.empty())
            {
                cond_var_.wait(lc, [&] { return quit_ || !timeouts_.empty(); });
            }

            while (!timeouts_.empty())
            {
                auto  min_next_run = timeouts_.get<BY_EXPIRATION>().begin()->expiration;

                if (cond_var_.wait_until(lc, min_next_run, [this] { return quit_ || fall_through_; }))
                {

                    fall_through_ = false;

                    break;
                }
                else
                {
                    //timeout
                    auto& by_expiration = timeouts_.get<BY_EXPIRATION>();
                    const auto end = by_expiration.upper_bound(min_next_run);
                    std::vector<TimeEvent> v_expired;
                    std::move(by_expiration.begin(), end, std::back_inserter(v_expired));
                    by_expiration.erase(by_expiration.begin(), end);
                    for (const auto& work : v_expired)
                    {
                        if (work.repeat_interval.count() >= 0)
                        {
                            by_expiration.insert(TimeEvent{ work.id, work.expiration + work.repeat_interval,work.repeat_interval,work.callback });
                        }
                    }

                    lc.unlock();


                    for (auto& work : v_expired)
                    {
                        new (&data[0]) boost::function<void()>(std::move(work.callback));
                        while (!task_queue_.push(data))continue;
                    }

                    lc.lock();

                }
            }
        }

    }

    boost::lockfree::queue<std::array<char, sizeof(boost::function<void()>)>> task_queue_;

    boost::scoped_ptr<boost::thread> task_handle_thread_;
    boost::scoped_ptr<boost::thread> timer_handle_thread_;
    bool start_ = false;
    bool quit_ = true;

    boost::mutex mtx_;
    boost::condition_variable cond_var_;

    struct TimeEvent
    {
        uint64_t id;
        boost::chrono::steady_clock::time_point expiration;
        boost::chrono::nanoseconds repeat_interval;
        std::function<void()> callback;


        TimeEvent(const TimeEvent& o) = default;
        TimeEvent& operator=(const TimeEvent& o) = default;

        TimeEvent(uint64_t id, boost::chrono::steady_clock::time_point expiration, boost::chrono::nanoseconds repeat_interval, std::function<void()> callback)
        {
            this->id = id;
            this->expiration = expiration;
            this->repeat_interval = repeat_interval;
            this->callback = std::move(callback);
        }

        TimeEvent(TimeEvent&& o)
        {
            id = o.id;
            expiration = o.expiration;
            repeat_interval = o.repeat_interval;
            callback = std::move(o.callback);
        }

        TimeEvent& operator=(TimeEvent&& o)
        {
            if (this != &o)
            {
                id = o.id;
                expiration = o.expiration;
                repeat_interval = o.repeat_interval;
                callback = std::move(o.callback);
            }
        }
    };

    typedef boost::multi_index_container<
        TimeEvent,
        boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
        boost::multi_index::member<TimeEvent, uint64_t, &TimeEvent::id>>,
        boost::multi_index::ordered_non_unique<
        boost::multi_index::member<TimeEvent, boost::chrono::steady_clock::time_point, &TimeEvent::expiration>>>>
        TimerSet;

    enum
    {
        BY_ID = 0,
        BY_EXPIRATION = 1,
    };

    TimerSet timeouts_;
    boost::atomic<uint64_t> next_id_;
    bool fall_through_ = false;

};

#endif
