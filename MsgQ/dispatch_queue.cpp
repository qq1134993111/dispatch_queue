#include "stdafx.h"
#include "dispatch_queue.h"
#include<algorithm>
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
	//std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);
	//work_queue_cond_.notify_one();
	work_queue_thread_started = true;

	//decltype(work_queue_) dq;
	std::vector<Event_Entry> vEvent(10240);
	while (quit_ == false)
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
	std::unique_lock< decltype(timer_mtx_) > timer_lock(timer_mtx_);
	timer_cond_.notify_one();
	timer_thread_started_ = true;

	while (quit_ == false)
	{
		if (timers_set_.empty())
		{
			timer_cond_.wait(timer_lock, [&] { return quit_ || !timers_set_.empty(); });
		}

		while (!timers_set_.empty())
		{
			auto  work = *timers_set_.begin();

			if (timer_cond_.wait_until(timer_lock, work.next_run_, [this] { return quit_.load() || fall_through_.load(); }))
			{
				//�ȴ��������

				fall_through_ = false;

				break;
			}
			else
			{
				//timeout
				timers_set_.erase(timers_set_.begin());
				timer_lock.unlock();
				//work.event_handler_();
				{
					//�������
					/*std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);

					auto where = std::find_if(work_queue_.rbegin(),
						work_queue_.rend(),
						[](Event_Entry const &e) { return !e.from_timer_; });

					work_queue_.insert(where.base(), work);

					work_queue_cond_.notify_one();*/
					work_concurrentqueue_.enqueue(work);

				}

				timer_lock.lock();
				if (work.repeat_)
				{
					work.next_run_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(work.timeout_);

					timers_set_.insert(std::move(work));
				}

			}
		}
	}

}
