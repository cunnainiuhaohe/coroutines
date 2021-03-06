// (c) 2013 Maciej Gajewski, <maciej.gajewski0@gmail.com>
#include "coroutines/scheduler.hpp"
#include "coroutines/algorithm.hpp"

//#define CORO_LOGGING
#include "coroutines/logging.hpp"

#include <boost/thread/locks.hpp>

#include <cassert>
#include <iostream>
#include <cstdlib>

namespace coroutines {

scheduler::scheduler(unsigned active_processors)
    : _active_processors(active_processors)
    , _processors()
    , _processors_mutex("sched processors mutex")
    , _starved_processors_mutex("sched starved processors mutex")
    , _coroutines_mutex("sched coroutines mutex")
    , _global_queue_mutex("sched global q mutex")
    , _random_generator(std::random_device()())
{
    assert(active_processors > 0);

    // setup
    {
        std::lock_guard<shared_mutex> lock(_processors_mutex);
        for(unsigned i = 0; i < active_processors; i++)
        {
            _processors.emplace_back(*this);
        }
    }
}

scheduler::~scheduler()
{
    wait();
    {
        std::lock_guard<shared_mutex> lock(_processors_mutex);
        _processors.stop_all();
    }
    CORO_LOG("SCHED: destroyed");
}

void scheduler::debug_dump()
{
    std::lock(_coroutines_mutex, _processors_mutex);

    std::cerr << "=========== scheduler debug dump ============" << std::endl;
    std::cerr << "          active coroutines now: " << _coroutines.size() << std::endl;
    std::cerr << "     max active coroutines seen: " << _max_active_coroutines << std::endl;
    std::cerr << "               no of processors: " << _processors.size();
    std::cerr << "       no of blocked processors: " << _blocked_processors;

    std::cerr << std::endl;
    std::cerr << " Active coroutines:" << std::endl;
    for(auto& coro : _coroutines)
    {
        std::cerr << " * " << coro->name() << " : " << coro->last_checkpoint() << std::endl;
    }
    std::cerr << "=============================================" << std::endl;
    std::terminate();
}

void scheduler::wait()
{
    CORO_LOG("SCHED: waiting...");

    std::unique_lock<mutex> lock(_coroutines_mutex);
    _coro_cv.wait(lock, [this]() { return _coroutines.empty(); });

    CORO_LOG("SCHED: wait over");
}

void scheduler::coroutine_finished(coroutine* coro)
{
    CORO_LOG("SCHED: coro=", coro, " finished");

    std::lock_guard<mutex> lock(_coroutines_mutex);
    auto it = find_ptr(_coroutines, coro);
    assert(it != _coroutines.end());
    _coroutines.erase(it);

    if (_coroutines.empty())
    {
        _coro_cv.notify_all();
    }
}

void scheduler::processor_starved(processor* pc)
{
    CORO_LOG("SCHED: processor ", pc, " starved");

    // step 1 - try to feed him global q
    {
        std::lock_guard<mutex> lock(_global_queue_mutex);

        if (!_global_queue.empty())
        {
            CORO_LOG("SCHED: scheduleing ", _global_queue.size(), " coros from global queue");
            pc->enqueue_or_die(_global_queue.begin(), _global_queue.end());
            _global_queue.clear();
            return;
        }
    }

    // step 2 - try to steal something
    {
        reader_guard<shared_mutex> lock(_processors_mutex);

        unsigned index = _processors.index_of(pc);
        if (index < _active_processors + _blocked_processors)
        {
            // try to steal
            unsigned most_busy = _processors.most_busy_index(0, _active_processors);
            std::vector<coroutine_weak_ptr> stolen;
            _processors[most_busy].steal(stolen);
            // if stealing successful - reactivate the processor
            if (!stolen.empty())
            {
                CORO_LOG("SCHED: stolen ", stolen.size(), " coros for proc=", pc, " from proc=", &_processors[most_busy]);
                pc->enqueue_or_die(stolen.begin(), stolen.end());
                return;
            }
        }
        // else: I don't care, you are in exile
        else
        {
            return;
        }
    }

    // record as starved
    {
        std::lock_guard<mutex> lock(_starved_processors_mutex);

        _starved_processors.push_back(pc);
    }
}

void scheduler::processor_blocked(processor_weak_ptr pc, std::vector<coroutine_weak_ptr>& queue)
{
    // move to blocked, schedule coroutines
    {
        // TODO use reader here
        boost::upgrade_lock<shared_mutex> lock(_processors_mutex);

        CORO_LOG("SCHED: proc=", pc, " blocked");

        _blocked_processors++;

        if (_processors.size() < _active_processors + _blocked_processors)
        {
            boost::upgrade_to_unique_lock<shared_mutex> upgrade_lock(lock);
            _processors.emplace_back(*this);
        }
    }
    // the procesor will now continue in blocked state


    // schedule coroutines
    schedule(queue.begin(), queue.end());
}

void scheduler::processor_unblocked(processor_weak_ptr pc)
{
    // TODO use reader here, upgrade if needed
    boost::upgrade_lock<shared_mutex> lock(_processors_mutex);

    CORO_LOG("SCHED: proc=", pc, " unblocked");

    assert(_blocked_processors > 0);
    _blocked_processors--;

    if (_processors.size() > _active_processors*3 + _blocked_processors) // if above high-water mark
    {
        std::lock_guard<mutex> starved_lock(_starved_processors_mutex);
        boost::upgrade_to_unique_lock<shared_mutex> upgrade_lock(lock);

        while(_processors.size() > _active_processors*2 + _blocked_processors) // recduce to acceptable value
        {
            CORO_LOG("SCHED: processors: ", _processors.size(), ", blocked: ", _blocked_processors, ", cleaning up");
            if (_processors.back().stop_if_idle())
            {
                _starved_processors.erase(
                    std::remove(_starved_processors.begin(), _starved_processors.end(), &_processors.back()),
                    _starved_processors.end());

                _processors.pop_back();
            }
            else
            {
                break; // some task is running, we'll come for it the next time
            }
        }
    }
}

// returns uniform random number between 0 and _max_allowed_running_coros
unsigned scheduler::random_index()
{
    std::uniform_int_distribution<unsigned> dist(0, _active_processors+_blocked_processors-1);
    return dist(_random_generator);
}


void scheduler::schedule(coroutine_weak_ptr coro)
{
    schedule(&coro, &coro + 1);
}

template<typename InputIterator>
void scheduler::schedule(InputIterator first,  InputIterator last)
{
    if (first == last)
        return; // that was easy :)

    CORO_LOG("SCHED: scheduling ", std::distance(first, last), " corountines. First:  '", (*first)->name(), "'");

    // step 1 - try adding to starved processor
    {
        std::lock_guard<mutex> lock(_starved_processors_mutex);

        if (!_starved_processors.empty())
        {
            CORO_LOG("SCHED: scheduling corountine, will add to starved processor");
            processor_weak_ptr starved = _starved_processors.back();
            _starved_processors.pop_back();
            starved->enqueue_or_die(first, last);
            return;
        }
    }

    // step 2 - add to self
    if (processor::current_processor() && processor::current_processor()->enqueue(first, last))
    {
        CORO_LOG("SCHED: scheduling corountine, added to self");
        return;
    }

    // total failure, add to global queue?
    {
        std::lock_guard<mutex> lock(_global_queue_mutex);

        CORO_LOG("SCHED: scheduling corountines, added to global queue");
        _global_queue.insert(_global_queue.end(), first, last);
    }
}

template
void scheduler::schedule<std::vector<coroutine_weak_ptr>::iterator>(std::vector<coroutine_weak_ptr>::iterator, std::vector<coroutine_weak_ptr>::iterator);

void scheduler::go(coroutine_ptr&& coro)
{
    CORO_LOG("SCHED: go '", coro->name(), "'");
    coroutine_weak_ptr coro_weak = coro.get();
    {
        std::lock_guard<mutex> lock(_coroutines_mutex);

        _coroutines.emplace_back(std::move(coro));
        _max_active_coroutines = std::max(_coroutines.size(), _max_active_coroutines);
    }

    schedule(coro_weak);
}

} // namespace coroutines
