#pragma once
#include<chrono>

class TimeElapsed
{
public:
	TimeElapsed() :start_point_(std::chrono::high_resolution_clock::now()), previous_point_(start_point_), current_point_(start_point_)
	{
		//start_=std::chrono::high_resolution_clock::now();
	}
	~TimeElapsed() {}

	void Restart()
	{
		current_point_ = previous_point_ = start_point_ = std::chrono::high_resolution_clock::now();
	}
	uint64_t Elapsed()
	{
		previous_point_ = current_point_;
		current_point_ = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(current_point_ - start_point_);
		return duration.count();
	}

	uint64_t ElapsedBetween()
	{
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(current_point_ - previous_point_);
		return duration.count();
	}

private:
	std::chrono::time_point<std::chrono::high_resolution_clock> start_point_;
	std::chrono::time_point<std::chrono::high_resolution_clock> previous_point_;
	std::chrono::time_point<std::chrono::high_resolution_clock> current_point_;

};
