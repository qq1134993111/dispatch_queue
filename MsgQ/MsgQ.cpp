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


void TestMsg()
{
    daily_logger->info("TestMsg");
}

int TestMsgSync()
{
    std::cout << "TestMsgSync \n";
    return 42;
}
int TestMsgSyncReturn(int i)
{
    return i;
}

void TestTimer(uint32_t index)
{
    //daily_logger->info("TestTimer:{}",index);
    //daily_logger->flush();
    static auto start = std::chrono::system_clock::now();
    auto end = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    start = end;

    static uint64_t nTimes = -1;
    static uint64_t nTotal = 0;
    static uint64_t nMax = std::numeric_limits<uint64_t>::min(), nMin = std::numeric_limits<uint64_t>::max(), nAvg = 0;

    if (++nTimes > 0)
    {
        uint64_t dur = abs(duration.count());
        if (dur > nMax)
        {
            nMax = dur;
        }

        if (dur < nMin)
        {
            nMin = dur;
        }

        nTotal += dur;
        nAvg = nTotal / nTimes;
    }

    daily_logger->info("TestTimer{}:times:{},cur:{},max:{},min:{},avg:{}", index, nTimes, duration.count(), nMax, nMin, nAvg);
    //daily_logger->flush();

}

int main()
{
    spdlog::set_pattern("*** [%Y-%m-%d %H:%M:%S,%f] %v ***");
    daily_logger->info("main");

    for (int i = 0; i < 10; i++)
    {
        DispatchQueue::GetDefaultDispatchQueue().DispatchAsync(TestMsg);
        DispatchQueue::GetDefaultDispatchQueue().DispatchSync(TestMsgSync);
        DispatchQueue::GetDefaultDispatchQueue().DispatchSync(TestMsgSyncReturn, i);
        auto f0 = DispatchQueue::GetDefaultDispatchQueue().Dispatch(TestMsgSync);
        std::cout << "Dispatch:" << f0.get() << "\n";
        auto f = DispatchQueue::GetDefaultDispatchQueue().Dispatch(TestMsgSyncReturn, i);
        std::cout << "Dispatch:" << f.get() << "\n";
    }

    DispatchQueue::GetDefaultDispatchQueue().SetTimer(100, true, TestTimer, 100);
    DispatchQueue::GetDefaultDispatchQueue().Join();

    return 0;
}

