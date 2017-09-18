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
class DispatchQueue
{
public:
	using  EventFunc = std::function<void()>;
	using time_point = std::chrono::steady_clock::time_point;

	struct Event_Entry
	{
		uint64_t id_;
		uint64_t timeout_;
		time_point next_run_;
		bool repeat_;
		EventFunc event_handler_;
		bool from_timer_;
		Event_Entry() {}
		Event_Entry(uint64_t id,
			uint64_t timeout,
			time_point next_run,
			bool repeat,
			EventFunc event_handler,
			bool from_timer = true) :
			id_(id),
			timeout_(timeout),
			next_run_(next_run),
			repeat_(repeat),
			event_handler_(event_handler),
			from_timer_(from_timer)
		{
		}

		Event_Entry(const Event_Entry &o)
		{
			this->id_ = o.id_;
			this->timeout_ = o.timeout_;
			this->next_run_ = o.next_run_;
			this->repeat_ = o.repeat_;
			this->event_handler_ = o.event_handler_;
			this->from_timer_ = o.from_timer_;
		}
		Event_Entry(Event_Entry &&o)
		{
			this->id_ = o.id_;
			this->timeout_ = o.timeout_;
			this->next_run_ = o.next_run_;
			this->repeat_ = o.repeat_;
			this->event_handler_ = std::move(o.event_handler_);
			this->from_timer_ = o.from_timer_;
		}
		Event_Entry & operator=(const Event_Entry &o)
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
		Event_Entry & operator=(Event_Entry &&o)
		{
			if (this != &o)
			{
				this->id_ = o.id_;
				this->timeout_ = o.timeout_;
				this->next_run_ = o.next_run_;
				this->repeat_ = o.repeat_;
				this->event_handler_ = std::move(o.event_handler_);
				this->from_timer_ = o.from_timer_;
			}

			return *this;
		}

	};
	struct CompareLessNextRun
	{
		bool operator()(const Event_Entry & left, const Event_Entry & right) const
		{
			return left.next_run_ < right.next_run_;
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

	DispatchQueue(const DispatchQueue &) = delete;
	DispatchQueue(DispatchQueue &&) = delete;
	DispatchQueue & operator=(const DispatchQueue &) = delete;
	DispatchQueue & operator=(DispatchQueue &&) = delete;

	void DispatchAsync(std::function< void() > func);
	template<class F, class... Args>
	void DispatchAsync(F&& f, Args&&... args)
	{
		auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
		std::function<void()> task = func;
		DispatchAsync(std::move(task));
	}
	
	template<class F, class... Args>
	void DispatchSync(F&& f, Args&&... args)
	{
		auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
		std::function<void()> task = func;
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


	uint64_t SetTimer(uint64_t milliseconds_timeout, EventFunc fun, bool repeat = true);

	template<class F, class... Args>
	uint64_t SetTimer(uint64_t milliseconds_timeout, bool repeat, F&& f, Args&&... args)
	{
		auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
		std::function<void()> task = func;

		return SetTimer(milliseconds_timeout, task, repeat);
	}
	bool CancelTimer(uint64_t timer_id);
	bool IsRunning()
	{
		return (timer_thread_started_&&work_queue_thread_started && !quit_);
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

			//std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);
			std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);
			//work_queue_cond_.wait(work_queue_lock, [this] { return work_queue_thread_started.load(); });
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

	std::atomic< bool > work_queue_thread_started;
	std::atomic< bool > timer_thread_started_;
	std::atomic<bool> quit_;

	/*std::mutex work_queue_mtx_;
	std::condition_variable work_queue_cond_;
	std::deque< Event_Entry> work_queue_;*/
	moodycamel::ConcurrentQueue<Event_Entry> work_concurrentqueue_;

	std::mutex timer_mtx_;
	std::condition_variable timer_cond_;
	std::multiset<Event_Entry, CompareLessNextRun> timers_set_;

	uint64_t generate_timer_id_;
	std::atomic<bool> fall_through_;

};

#endif
