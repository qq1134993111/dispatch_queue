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
	daily_logger->info("TestTimer:{}",index);
	daily_logger->flush();
}

int main()
{
	spdlog::set_pattern("*** [%Y-%m-%d %H:%M:%S,%f] %v ***");
	daily_logger->info("main");
	g_dspq.SetTimer(1000, true, TestTimer, 1000);
	g_dspq.SetTimer(1500, true, TestTimer, 1500);
	for (int i = 0; i < 10000; i++)
	{
		g_dspq.DispatchAsync(TestMsg);
	}
	g_dspq.SetTimer(2000, true, TestTimer, 2000);
	g_dspq.SetTimer(50, true, TestTimer, 50);
	g_dspq.Join();
    return 0;
}

