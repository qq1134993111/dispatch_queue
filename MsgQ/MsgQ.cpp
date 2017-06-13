// MsgQ.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#include "spdlog/spdlog.h"

#include <iostream>
#include <memory>

#include "dispatch_queue.h"
namespace chrono = std::chrono;
namespace spd = spdlog;

auto daily_logger = spd::daily_logger_mt("daily_logger", "daily", 2, 30);

DispatchQueue g_dspq;

void TestMsg()
{
	daily_logger->info("TestMsg");
}

void TestTimer(uint32_t index)
{
	//daily_logger->info("TestTimer:{}",index);
	//daily_logger->flush();
	static auto start = std::chrono::system_clock::now();	auto end = std::chrono::system_clock::now();	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);	start = end;	static uint32_t nTimes = -1;	static uint64_t nTotal = 0;	static uint32_t nMax = std::numeric_limits<uint32_t>::min(), nMin = std::numeric_limits<uint32_t>::max(), nAvg = 0;	if (++nTimes > 0)	{		auto dur = abs(duration.count());		if (dur > nMax)		{			nMax = dur;		}		if (dur < nMin)		{			nMin = dur;		}		nTotal += dur;		nAvg = nTotal / nTimes;	}	daily_logger->info("TestTimer{}:times:{},cur:{},max:{},min:{},avg:{}", index, nTimes, duration.count(), nMax, nMin, nAvg);	//daily_logger->flush();
}

int main()
{
	spdlog::set_pattern("*** [%Y-%m-%d %H:%M:%S,%f] %v ***");
	daily_logger->info("main");
	//g_dspq.SetTimer(1000, true, TestTimer, 1000);
	//g_dspq.SetTimer(1500, true, TestTimer, 1500);
	for (int i = 0; i < 10000; i++)
	{
		g_dspq.DispatchAsync(TestMsg);
	}
	//g_dspq.SetTimer(2000, true, TestTimer, 2000);
	//g_dspq.SetTimer(50, true, TestTimer, 50);
	g_dspq.SetTimer(100, true, TestTimer, 100);
	g_dspq.Join();
    return 0;
}

