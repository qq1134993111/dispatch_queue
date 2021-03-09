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

class DispatchQueue
{
public:
	using  EventFunc = std::function<void()>;
	using time_point = std::chrono::steady_clock::time_point;

	enum class EntryType :uint8_t
	{
		kMsg = 0,
		kSingleTimer,
		kMultipleTimer
	};

	struct EventEntry
	{
		EntryType type_;
		uint64_t id_;
		uint64_t timeout_;
		time_point next_run_;
		EventFunc event_handler_;

		EventEntry(EntryType type,
			uint64_t id,
			uint64_t timeout,
			time_point next_run,
			EventFunc event_handler) :
			type_(type),
			id_(id),
			timeout_(timeout),
			next_run_(next_run),
			event_handler_(std::move(event_handler))
		{

		}

		EventEntry(const EventEntry& o)
		{
			this->type_ = o.type_;
			this->id_ = o.id_;
			this->timeout_ = o.timeout_;
			this->next_run_ = o.next_run_;
			this->event_handler_ = o.event_handler_;

		}
		EventEntry(EventEntry&& o)
		{
			this->type_ = o.type_;
			this->id_ = o.id_;
			this->timeout_ = o.timeout_;
			this->next_run_ = o.next_run_;
			this->event_handler_ = std::move(o.event_handler_);
		}
		EventEntry& operator=(const EventEntry& o)
		{
			if (this != &o)
			{
				this->type_ = o.type_;
				this->id_ = o.id_;
				this->timeout_ = o.timeout_;
				this->next_run_ = o.next_run_;
				this->event_handler_ = o.event_handler_;
			}

			return *this;
		}
		EventEntry& operator=(EventEntry&& o)
		{
			if (this != &o)
			{
				this->type_ = o.type_;
				this->id_ = o.id_;
				this->timeout_ = o.timeout_;
				this->next_run_ = o.next_run_;
				this->event_handler_ = std::move(o.event_handler_);
			}

			return *this;
		}

	};
	struct CompareLessNextRun
	{
		bool operator()(const EventEntry& left, const EventEntry& right) const
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

	DispatchQueue(const DispatchQueue&) = delete;
	DispatchQueue(DispatchQueue&&) = delete;
	DispatchQueue& operator=(const DispatchQueue&) = delete;
	DispatchQueue& operator=(DispatchQueue&&) = delete;

	/*
	 * []()->void{}
	 */
	template<typename Fn>
	void DispatchAsync(Fn&& func)
	{
		std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);
		work_queue_.push_front(EventEntry(EntryType::kMsg, 0, 0, time_point(), std::forward<Fn>(func)));
		work_queue_cond_.notify_one();
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
			std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);

			work_queue_.push_front(EventEntry(EntryType::kMsg, 0, 0, time_point(), std::forward<Fn>(func)));

			work_queue_.push_front(EventEntry(EntryType::kMsg, 0, 0, time_point(), [&]()
				{
					std::unique_lock<std::mutex> sync_cb_lock(sync_mtx);
					completed = true;
					sync_cond.notify_one();

				}));

			work_queue_cond_.notify_one();
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


	uint64_t SetTimer(uint64_t milliseconds_timeout, EventFunc fun, bool repeat = true);

	template<class F, class... Args>
	uint64_t SetTimer(uint64_t milliseconds_timeout, bool repeat, F&& f, Args&&... args)
	{
		std::function<void()> task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

		return SetTimer(milliseconds_timeout, task, repeat);
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

			work_queue_thread_ = std::thread(&DispatchQueue::DispatchThreadProc, this);

			std::thread t1(&DispatchQueue::TimerThreadProc, this);
			timer_thread_ = std::move(t1);

			std::unique_lock< decltype(work_queue_mtx_) >  work_queue_lock(work_queue_mtx_);
			std::unique_lock<decltype(timer_mtx_)> timer_lock(timer_mtx_);
			work_queue_cond_.wait(work_queue_lock, [this] { return work_queue_thread_started.load(); });
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

	std::mutex work_queue_mtx_;
	std::condition_variable work_queue_cond_;
	std::deque< EventEntry> work_queue_;

	std::mutex timer_mtx_;
	std::condition_variable timer_cond_;
	std::multiset<EventEntry, CompareLessNextRun> timers_set_;

	uint64_t generate_timer_id_;
	std::atomic<bool> fall_through_;

};

#endif
