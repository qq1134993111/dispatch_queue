// MsgQ.cpp : 定义控制台应用程序的入口点。
//


#include "spdlog/spdlog.h"

#include <iostream>
#include <memory>

#include "dispatch_queue.h"
namespace chrono = std::chrono;
namespace spd = spdlog;

auto daily_logger = spd::daily_logger_mt("daily_logger", "daily", 2, 30);

#include"Test.h"

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
    daily_logger->flush();
    static int i = 0;
    std::cout << ++i << "\n";
}

struct MyStruct
{
    MyStruct() {
        std::cout << "MyStruct\n";
    }
    ~MyStruct() {
        std::cout << "~MyStruct\n";
    }
};

int main()
{
    if (1)
    {
        std::cout << "TestDispatchQueue\n";
        TestDispatchQueue(1000000, 0);
        std::cout << "TestAsio\n";
        TestAsio(1000000, 0);
        std::cout << "TestStrand\n";
        TestStrand(1000000, 0);
        std::cout << "TestAsio2\n";
        TestAsio2(1000000, 0);
        std::cout << "TestStrand2\n";
        TestStrand2(1000000, 0);
    }

    if (0)
    {
        std::cout << "TestDispatchQueue\n";
        TestDispatchQueue(10000, 10000);
        std::cout << "TestAsio\n";
        TestAsio(10000, 10000);
        std::cout << "TestStrand\n";
        TestStrand(10000, 10000);
        std::cout << "TestAsio2\n";
        TestAsio2(10000, 10000);
        std::cout << "TestStrand2\n";
        TestStrand2(10000, 10000);
    }

    spdlog::set_pattern("*** [%Y-%m-%d %H:%M:%S,%f] %v ***");
    daily_logger->info("main");

    DispatchQueue::GetDefaultDispatchQueue().Start();

    if (1)
    {
        auto p = std::make_shared<MyStruct>();
        DispatchQueue::GetDefaultDispatchQueue().DispatchAsync([p] {
            std::cout << "hello\n";
            });
        DispatchQueue::GetDefaultDispatchQueue().DispatchAsync([p] {
            std::cout << "world\n";
            });
    }

    for (int i = 0; i < 10; i++)
    {
        DispatchQueue::GetDefaultDispatchQueue().DispatchAsync(&TestMsg);

        auto f0 = DispatchQueue::GetDefaultDispatchQueue().Dispatch(TestMsgSync);
        std::cout << "Dispatch:" << f0.get() << "\n";
        auto f = DispatchQueue::GetDefaultDispatchQueue().Dispatch(TestMsgSyncReturn, i);
        std::cout << "Dispatch:" << f.get() << "\n";
    }

    auto id = DispatchQueue::GetDefaultDispatchQueue().SetTimer(boost::chrono::seconds(1), true, TestTimer, 100);
    std::cout << "ID:" << id << "\n";
    std::this_thread::sleep_for(std::chrono::seconds(60));

    std::cout << "cancel :" << id << "\n";
    DispatchQueue::GetDefaultDispatchQueue().CancelTimer(id);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 0;
}

