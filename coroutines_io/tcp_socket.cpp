// Copyright (c) 2013 Maciej Gajewski

#include "tcp_socket.hpp"
#include "service.hpp"
#include "globals.hpp"

#include <sys/socket.h>
#include <netinet/in.h>

#include <cstring>

namespace coroutines {

tcp_socket::tcp_socket(coroutines::service& srv)
    : _service(srv)
{
}

tcp_socket::tcp_socket()
    : _service(get_service_check())
{
}

tcp_socket::~tcp_socket()
{
}

void tcp_socket::connect(const tcp_socket::endpoint_type& endpoint)
{
    auto pair = _service.get_scheduler().make_channel<boost::system::error_code>(1);
    _reader = std::move(pair.reader);
    _writer = std::move(pair.writer);

    int af = endpoint.address().is_v4() ? AF_INET : AF_INET6;

    open(af);

    int cr = 0;
    if (af == AF_INET)
    {
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));

        addr.sin_family = af;
        addr.sin_addr.s_addr = ::htonl(endpoint.address().to_v4().to_ulong());
        addr.sin_port = ::htons(endpoint.port());
        cr = ::connect(_socket, (sockaddr*)&addr, sizeof(addr));
    }
    else
    {
        assert(true); // not implemented
    }

    if (cr == 0 )
    {
        assert(false);
    }
    if (cr != EINPROGRESS)
    {
        throw boost::system::system_error(
            boost::system::error_code(
                errno,
                boost::asio::error::get_system_category()
                )
            );
    }

    // todo register with epoll
    // wait_for_readable(_socket);

}

void tcp_socket::open(int address_family)
{
    _socket = ::socket(
        address_family,
        SOCK_STREAM | SOCK_NONBLOCK,
        0);

    if (_socket == -1)
    {
        throw boost::system::system_error(
            boost::system::error_code(
                errno,
                boost::asio::error::get_system_category()
                )
            );
    }
}



}
