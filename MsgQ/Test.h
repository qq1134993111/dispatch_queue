#pragma once
#include <algorithm>
#include <tuple>
#include "dispatch_queue.h"
#include "boost/asio.hpp"
struct MyDelayInfo
{
    std::chrono::high_resolution_clock::time_point s;
    std::chrono::high_resolution_clock::time_point m;
    std::chrono::high_resolution_clock::time_point e;
};

void Tongji(std::vector<MyDelayInfo>& v_data)
{
    std::vector<std::tuple<int64_t, int64_t, int64_t>> v_dealy;
    int64_t  total_delay = 0, push_delay = 0, pop_delay = 0;
    for (auto& v : v_data)
    {
        if (v.e < v.s)
            std::cout << "s>e data:"<<v.s.time_since_epoch().count()<<","<<v.e.time_since_epoch().count()<<"\n";

        auto push_item = std::chrono::duration_cast<std::chrono::nanoseconds>(v.m - v.s).count();
        auto pop_item = std::chrono::duration_cast<std::chrono::nanoseconds>(v.e - v.m).count();
        auto item = std::chrono::duration_cast<std::chrono::nanoseconds>(v.e - v.s).count();
        push_delay += push_item;
        pop_delay += pop_item;
        total_delay += item;
        v_dealy.push_back(std::make_tuple(item,push_item, pop_item));
    }

    std::cout << "count:" << v_data.size() << ",total_avg:" << total_delay / v_data.size() << ",push_avg:" << push_delay / v_data.size() << ",pop_avg:" << pop_delay / v_data.size() << "\n";
    
    std::sort(v_dealy.begin(), v_dealy.end(), [](auto&& t1,auto&& t2) {
        return std::get<0>(t1) < std::get<0>(t2);
        });

  
    auto fun_cout_index=[&](int64_t index)
    {
        auto& v = v_dealy[index];
        std::stringstream ss;
        ss<<std::get<0>(v) << "," << std::get<1>(v) << "," << std::get<2>(v)<<"\n";
        return ss.str();
    };

    std::cout << "min:"<< fun_cout_index(0);
    std::cout << "%10:" << fun_cout_index(v_dealy.size()*10/100);
    std::cout << "%30:" << fun_cout_index(v_dealy.size() * 30 / 100);
    std::cout << "%50:" << fun_cout_index(v_dealy.size() * 50 / 100);
    std::cout << "%70:" << fun_cout_index(v_dealy.size() * 70 / 100);
    std::cout << "%90:" << fun_cout_index(v_dealy.size() * 90 / 100);
    std::cout << "%95:" << fun_cout_index(v_dealy.size() * 95 / 100);
    std::cout << "%97:" << fun_cout_index(v_dealy.size() * 97 / 100);
    std::cout << "%99:" << fun_cout_index(v_dealy.size() * 99 / 100);
    std::cout << "%99.9:" << fun_cout_index(v_dealy.size() * 99.9 / 100);
    std::cout << "max:" << fun_cout_index(v_dealy.size() - 1);

    auto fun_cout_index_avg = [&](int64_t index)
    {
        int64_t total = 0;
        for (int i = 0; i < index; i++)
        {
            total += std::get<0>(v_dealy[i]);
        }

        std::stringstream ss;
        ss << total / index<<" (index"<<index << ")\n";
        return ss.str();
    };

    std::cout << "%99.9 avg:" << fun_cout_index_avg(v_dealy.size() * 99.9 / 100);
    std::cout << "%99 avg:" << fun_cout_index_avg(v_dealy.size() * 99 / 100);

}

void TestDispatchQueue(int count = 1000000, int64_t sleep_ns = 0)
{
    DispatchQueue queue;
    queue.Start();
    std::vector<MyDelayInfo> v_data;
    v_data.resize(count);
    for (int i = 0; i < count; i++)
    {
        if (sleep_ns)
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
        }

        v_data[i].s = std::chrono::high_resolution_clock::now();
        queue.DispatchAsync([&v_data, i]()
            {
                v_data[i].e = std::chrono::high_resolution_clock::now();
            });
        v_data[i].m = std::chrono::high_resolution_clock::now();
    }

    queue.Dispatch([]() { std::cout << "end\n"; });

    Tongji(v_data);
}


void TestAsio(int count = 1000000, int64_t sleep_ns = 0)
{
    boost::asio::io_service ios;
    auto woker = std::make_unique<boost::asio::io_service::work>(ios);

    std::thread  mythread([&]() 
    {
        boost::system::error_code ec;
        ios.run(ec);
    });

    std::atomic<bool> quit=false;

    std::vector<MyDelayInfo> v_data;
    v_data.resize(count);
    for (int i = 0; i < count; i++)
    {
        if (sleep_ns)
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
        }

        v_data[i].s = std::chrono::high_resolution_clock::now();
        ios.post([&v_data,i,count,&quit]() 
            {
                v_data[i].e = std::chrono::high_resolution_clock::now();
                if (i >= count - 1)
                {
                    std::cout <<i<< "end\n";
                    quit = true;
                }
            });

        v_data[i].m = std::chrono::high_resolution_clock::now();
    }

    while (!quit)
    {
        std::this_thread::yield();
    }

    woker.reset(nullptr);

    if (mythread.joinable())
    {
        mythread.join();
    }

    Tongji(v_data);
}

void TestStrand(int count = 1000000, int64_t sleep_ns = 0)
{
    boost::asio::io_service ios;
    auto woker = std::make_unique<boost::asio::io_service::work>(ios);
    boost::asio::io_service::strand mystrand(ios);
    std::thread  mythread([&]()
        {
            boost::system::error_code ec;
            ios.run(ec);
        });

    std::atomic<bool> quit = false;

    std::vector<MyDelayInfo> v_data;
    v_data.resize(count);
    for (int i = 0; i < count; i++)
    {
        if (sleep_ns)
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
        }

        v_data[i].s = std::chrono::high_resolution_clock::now();
        mystrand.post([&v_data, i, count, &quit]()
            {
                v_data[i].e = std::chrono::high_resolution_clock::now();
                if (i >= count - 1)
                {
                    std::cout <<i<< "end\n";
                    quit = true;
                }
            });

        v_data[i].m = std::chrono::high_resolution_clock::now();
    }

    while (!quit)
    {
        std::this_thread::yield();
    }

    woker.reset(nullptr);

    if (mythread.joinable())
    {
        mythread.join();
    }

    Tongji(v_data);
}

void TestAsio2(int count = 1000000, int64_t sleep_ns = 0)
{
    boost::asio::io_service ios;
    std::atomic<bool> quit = false;

    std::thread  mythread([&]()
        {
            boost::system::error_code ec;
            while (!quit)
            {
                ios.restart();
                ios.run(ec);
            }     
        });

  

    std::vector<MyDelayInfo> v_data;
    v_data.resize(count);
    for (int i = 0; i < count; i++)
    {
        if (sleep_ns)
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
        }

        v_data[i].s = std::chrono::high_resolution_clock::now();
        ios.post([&v_data, i, count, &quit]()
            {
                v_data[i].e = std::chrono::high_resolution_clock::now();
                if (i >= count - 1)
                {
                    std::cout << i<<"end\n";
                    quit = true;
                }
            });

        v_data[i].m = std::chrono::high_resolution_clock::now();
    }

    while (!quit)
    {
        std::this_thread::yield();
    }


    if (mythread.joinable())
    {
        mythread.join();
    }

    Tongji(v_data);
}

void TestStrand2(int count = 1000000, int64_t sleep_ns = 0)
{
    boost::asio::io_service ios;
    std::atomic<bool> quit = false;

    boost::asio::io_service::strand mystrand(ios);
    std::thread  mythread([&]()
        {
            boost::system::error_code ec;
            while (!quit)
            {
                ios.restart();
                ios.run(ec);
            }
        });

  

    std::vector<MyDelayInfo> v_data;
    v_data.resize(count);
    for (int i = 0; i < count; i++)
    {
        if (sleep_ns)
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
        }

        v_data[i].s = std::chrono::high_resolution_clock::now();
        mystrand.post([&v_data, i, count, &quit]()
            {
                v_data[i].e = std::chrono::high_resolution_clock::now();
                if (i >= count - 1)
                {
                    std::cout << i<<"end\n";
                    quit = true;
                }
            });

        v_data[i].m = std::chrono::high_resolution_clock::now();
    }

    while (!quit)
    {
        std::this_thread::yield();
    }

 
    if (mythread.joinable())
    {
        mythread.join();
    }

    Tongji(v_data);
}