#include "stdafx.h"
#include "dispatch_queue.h"
#include<algorithm>
#include <iterator>
#include <vector>
DispatchQueue::DispatchQueue() :timer_thread_started_(false), work_queue_thread_started(false), quit_(false), generate_timer_id_(0), fall_through_(false)
{
    InitThread();
}


DispatchQueue::~DispatchQueue()
{
    Stop();
    Join();
}


uint64_t DispatchQueue::SetTimer(uint64_t milliseconds_timeout, EventFunc fun, bool repeat)
{
    if (!IsRunning())
        return 0;

    std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);
    Event_Entry event_entry(++generate_timer_id_, milliseconds_timeout, std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds_timeout), repeat, std::move(fun), true);
    if (!timers_set_.empty() && event_entry.next_run_ < timers_set_.begin()->next_run_)
    {
        fall_through_ = true;
    }
    timers_set_.insert(std::move(event_entry));
    timer_cond_.notify_one();

    return generate_timer_id_;
}

bool DispatchQueue::CancelTimer(uint64_t timer_id)
{
    if (!IsRunning())
        return false;

    std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);
    auto fun = [&](const Event_Entry& event_entry) {  return event_entry.id_ == timer_id; };
    auto it = std::find_if(timers_set_.begin(), timers_set_.end(), fun);
    if (it != timers_set_.end())
    {
        if (it == timers_set_.begin())
        {
            timers_set_.erase(it);
            fall_through_ = true;
            timer_cond_.notify_one();

            return true;
        }
        else
        {
            timers_set_.erase(it);
            return true;
        }
    }
    else
    {
        return false;
    }

    return true;
}

void DispatchQueue::DispatchThreadProc()
{

    work_queue_thread_started = true;

    std::vector<Event_Entry> vEvent(10240);
    while (!quit_)
    {
        size_t len = work_concurrentqueue_.try_dequeue_bulk(vEvent.begin(), vEvent.size());
        for (size_t i = 0; i < len; i++)
        {
            if (vEvent[i].event_handler_)
            {
                vEvent[i].event_handler_();
            }
        }
    }

}

void DispatchQueue::TimerThreadProc()
{

    {
        timer_thread_started_ = true;
        timer_cond_.notify_one();
    }

    std::unique_lock< decltype(timer_mtx_) > timer_lock(timer_mtx_);

    while (!quit_)
    {
        if (timers_set_.empty())
        {
            timer_cond_.wait(timer_lock, [&] { return quit_ || !timers_set_.empty(); });
        }

        while (!timers_set_.empty())
        {
            auto  min_work = *timers_set_.begin();

            if (timer_cond_.wait_until(timer_lock, min_work.next_run_, [this] { return quit_.load() || fall_through_.load(); }))
            {
                //等待条件完成

                fall_through_ = false;

                break;
            }
            else
            {
                //timeout
                const auto end = timers_set_.upper_bound(min_work);
                std::vector<Event_Entry> v_expired;
                std::move(timers_set_.begin(), end, std::back_inserter(v_expired));
                timers_set_.erase(timers_set_.begin(), end);
                for (const auto& work : v_expired)
                {
                    if (work.repeat_)
                    {
                        timers_set_.insert(Event_Entry(work.id_, work.timeout_, work.next_run_ + std::chrono::milliseconds(work.timeout_), true, work.event_handler_, true));
                    }
                }
                timer_lock.unlock();

                //插入队列
                work_concurrentqueue_.enqueue_bulk(v_expired.begin(), v_expired.size());

                timer_lock.lock();

            }
        }
    }

}
