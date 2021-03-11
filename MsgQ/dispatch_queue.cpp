
#include "dispatch_queue.h"
#include<algorithm>
#include <iterator>
#include <vector>
DispatchQueue::DispatchQueue() :generate_timer_id_(0), fall_through_(false)
{
    //Start();
}


DispatchQueue::~DispatchQueue()
{
    Stop(true);
}



bool DispatchQueue::CancelTimer(uint64_t timer_id)
{
    if (!IsRunning())
        return false;

    std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);

    auto& by_id = timers_set_.get<BY_ID>();
    auto& by_expiration = timers_set_.get<BY_EXPIRATION>();

    auto it = by_id.find(timer_id);
    if (it != by_id.end())
    {
        if (it->id_ == by_expiration.begin()->id_)
        {
            by_id.erase(it);

            fall_through_ = true;
            timer_cond_.notify_one();
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

void DispatchQueue::DispatchThreadProc()
{
    std::vector<EventFunc> vEvent(10240);
    while (!quit_)
    {
        size_t len = work_concurrentqueue_.try_dequeue_bulk(vEvent.begin(), vEvent.size());
        for (size_t i = 0; i < len; i++)
        {
            if (vEvent[i])
            {
                vEvent[i]();
            }
        }
    }

}

void DispatchQueue::TimerThreadProc()
{

    std::unique_lock< decltype(timer_mtx_) > timer_lock(timer_mtx_);

    while (!quit_)
    {
        if (timers_set_.empty())
        {
            timer_cond_.wait(timer_lock, [&] { return quit_ || !timers_set_.empty(); });
        }

        while (!timers_set_.empty())
        {
            auto  min_next_run = timers_set_.get<BY_EXPIRATION>().begin()->next_run_;

            if (timer_cond_.wait_until(timer_lock, min_next_run, [this] { return quit_.load() || fall_through_; }))
            {
                //等待条件完成

                fall_through_ = false;

                break;
            }
            else
            {
                //timeout
                auto& by_expiration = timers_set_.get<BY_EXPIRATION>();
                const auto end = by_expiration.upper_bound(min_next_run);
                std::vector<EventEntry> v_expired;
                std::move(by_expiration.begin(), end, std::back_inserter(v_expired));
                by_expiration.erase(by_expiration.begin(), end);
                for (const auto& work : v_expired)
                {
                    if (work.repeat_)
                    {
                        //by_expiration.insert(EventEntry(work.id_, work.timeout_, work.next_run_ + work.timeout_, true, work.event_handler_));
                        by_expiration.emplace(work.id_, work.timeout_, work.next_run_ + work.timeout_, true, work.event_handler_);
                    }
                }
                timer_lock.unlock();

                //插入队列
                for (auto& work : v_expired)
                {
                    work_concurrentqueue_.enqueue(std::move(work.event_handler_));
                }

                timer_lock.lock();

            }
        }
    }

}
