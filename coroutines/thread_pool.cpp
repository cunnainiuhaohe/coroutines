// (c) 2013 Maciej Gajewski, <maciej.gajewski0@gmail.com>
#include "coroutines/thread_pool.hpp"
#include "coroutines/algorithm.hpp"
#include <cassert>
#include <iostream>

namespace coroutines {


void detail::parked_thread::routine()
{
    for(;;)
    {
        {
            std::unique_lock<std::mutex> lock(_mutex);

            _fn = nullptr;
            _running = false;
            _join_cv.notify_all();
            _cv.wait(lock, [this]() { return _fn != nullptr || _stopped; } );
            _running = true;
        }

        if (_stopped)
            return;

        assert(_fn);

        _fn();

    }
}

bool detail::parked_thread::run(std::function<void ()>&& fn)
{
    std::lock_guard<std::mutex> lock(_mutex);
    assert(!_stopped);

    if(!_running)
    {
        _fn = std::move(fn);
        _cv.notify_all();
        return true;
    }
    return false;
}

void detail::parked_thread::join()
{
    std::unique_lock<std::mutex> lock(_mutex);
    _join_cv.wait(lock, [this]() { return !_running; });
}

void detail::parked_thread::stop_and_join()
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stopped = true;
        _cv.notify_all();
    }
    _thread.join();
}


thread_pool::thread_pool(unsigned size)
{
    assert(size >= 1);
    _parked_threads.reserve(size);
    for(unsigned i = 0; i < size; i++)
    {
        _parked_threads.emplace_back();
    }
}

thread_pool::~thread_pool()
{
    stop_and_join();
}

void thread_pool::run(std::function<void()> fn)
{
    for(detail::parked_thread& parked : _parked_threads)
    {
        if(parked.run(std::move(fn)))
        {
            return;
        }
    }

    create_free_thread(std::move(fn));
    join_completed(); // garbage collection
}

void thread_pool::join()
{
    for(auto& parked : _parked_threads)
    {
        parked.join();
    }
    join_all_free_threads();
}

void thread_pool::stop_and_join()
{
    for(auto& parked : _parked_threads)
    {
        parked.stop_and_join();
    }
    join_all_free_threads();
}

void thread_pool::create_free_thread(std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lock(_free_threads_mutex);
        _free_threads.emplace_back(new detail::free_thread(std::move(fn)));
    }
}

void thread_pool::join_all_free_threads()
{
    std::lock_guard<std::mutex> lock(_free_threads_mutex);
    for(auto& tp : _free_threads)
    {
        tp->join();
    }
    _free_threads.clear();
}

void thread_pool::join_completed()
{
    std::lock_guard<std::mutex> lock(_free_threads_mutex);

    std::remove_if(
        _free_threads.begin(), _free_threads.end(),
        [](const detail::free_thread_ptr& tp)
        {
            return tp->finished();
        });
}




}
