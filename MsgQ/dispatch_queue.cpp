
#include "dispatch_queue.h"
#include<algorithm>

DispatchQueue::DispatchQueue() :timer_thread_started_(false), work_queue_thread_started(false),quit_(false), generate_timer_id_(0), fall_through_(false)
{
	InitThread();
}


DispatchQueue::~DispatchQueue()
{
	Stop();
	Join();
}

void DispatchQueue::DispatchAsync(std::function< void() > func)
{
	std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);
	work_queue_.push_front(EventEntry(EntryType::kMsg,0,0,time_point(), std::move(func)));
	work_queue_cond_.notify_one();
}

void DispatchQueue::DispatchSync(std::function<void()> func)
{
	std::mutex sync_mtx;
	std::unique_lock< decltype(sync_mtx) > sync_lock(sync_mtx);
	std::condition_variable sync_cond;
	std::atomic< bool > completed(false);

	{
		std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);

		work_queue_.push_front(EventEntry(EntryType::kMsg,0, 0, time_point(), move(func)));

		work_queue_.push_front(EventEntry(EntryType::kMsg,0, 0, time_point(), [&]() 
		{

			std::unique_lock<std::mutex> sync_cb_lock(sync_mtx);
			completed = true;
			sync_cond.notify_one();

		}));

		work_queue_cond_.notify_one();
	}

	sync_cond.wait(sync_lock, [&] { return completed.load(); });
}

uint64_t DispatchQueue::SetTimer(uint64_t milliseconds_timeout, EventFunc fun, bool repeat)
{
	if (!IsRunning())
		return 0;

	EntryType type;
	type = repeat ? EntryType::kMultipleTimer : EntryType::kSingleTimer;

	std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);
	EventEntry event_entry(type,++generate_timer_id_, milliseconds_timeout, std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds_timeout), std::move(fun));
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
	auto fun = [&](const EventEntry& event_entry) {  return event_entry.id_ == timer_id; };
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
	std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);
	work_queue_cond_.notify_one();
	work_queue_thread_started = true;

	decltype(work_queue_) dq;
	while (!quit_)
	{
		work_queue_cond_.wait(work_queue_lock, [&] { return !work_queue_.empty(); });
		/*	while (!work_queue_.empty())
			{
				auto work = std::move(work_queue_.back());
				work_queue_.pop_back();

				work_queue_lock.unlock();
				if (work.event_handler_)
					work.event_handler_();
				work_queue_lock.lock();
			}*/
		
		dq = std::move(work_queue_);
		work_queue_lock.unlock();
		for (auto &work : dq)
		{
			if (work.event_handler_)
				work.event_handler_();
		}
		work_queue_lock.lock();
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
				//等待条件完成

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
					//插入队列
					std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);

					auto where = std::find_if(work_queue_.rbegin(),
						work_queue_.rend(),
						[](EventEntry const &e) { return !(e.type_==EntryType::kSingleTimer|| e.type_ == EntryType::kMultipleTimer); });

					work_queue_.insert(where.base(), work);

					work_queue_cond_.notify_one();

				}

				timer_lock.lock();
				if (work.type_==EntryType::kMultipleTimer)
				{
					work.next_run_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(work.timeout_);

					timers_set_.insert(std::move(work));
				}

			}
		}
	}

}
