// Copyright (c) 2013 Maciej Gajewski

#include "coroutines_io/service.hpp"
#include "coroutines_io/globals.hpp"
#include "coroutines/globals.hpp"

#include "coroutines_io/detail/poller.hpp"

#include <unordered_map>

namespace coroutines {

service::service(scheduler& sched)
    : _scheduler(sched)
{
}

service::~service()
{
}

void service::wait_for_writable(int fd, const channel_writer<std::error_code>& writer)
{
    _command_writer.put(command{fd, FD_WRITABLE, writer});
    _poller.wake();
}

void service::wait_for_readable(int fd, const channel_writer<std::error_code>& writer)
{
    _command_writer.put(command{fd, FD_READABLE, writer});
    _poller.wake();
}

void service::start()
{
    auto pair = _scheduler.make_channel<command>(10);
    _command_writer = std::move(pair.writer);
    _command_reader = std::move(pair.reader);

    _scheduler.go("service loop", [this](){ loop(); });
}

void service::stop()
{
    _command_writer.close();
}

void service::loop()
{
    std::unordered_map<std::uint64_t, command> commands;
    std::uint64_t counter = 0;
    std::vector<std::uint64_t> keys;

    for(;;)
    {
        command cmd;

        // if no pending commands, block on reader
        if (commands.empty())
        {
            cmd = _command_reader.get();
            _poller.add_fd(cmd.fd, cmd.events, counter);
            commands.insert(std::make_pair(counter++, cmd));
        }

        //read all events
        while(_command_reader.try_get(cmd))
        {
            _poller.add_fd(cmd.fd, cmd.events, counter);
            commands.insert(std::make_pair(counter++, cmd));
        }

        // poll!
        keys.clear();
        block([&]()
        {
            _poller.wait(keys);
        });

        std::cout << "POLLER: " << keys.size() << " events ready" << std::endl;

        // serve events
        for(std::uint64_t key : keys)
        {
            auto it = commands.find(key);
            assert(it != commands.end());
            it->second.writer.put(std::error_code());
            commands.erase(it);
        }

    }


}

}
