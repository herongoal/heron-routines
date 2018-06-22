#include "tcp_passive_session_routine.h"
#include "circular_buffer.h"
#include "log_api.h"
#include "routine_proxy.h"
#include "service.h"


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>


#include <cstring>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>


using namespace std;
using std::vector;
using std::string;


namespace hrts{
tcp_passive_session_routine::tcp_passive_session_routine(routine_proxy *proxy, char category, int fd):
		routine(proxy, category, 0, fd)
{
	m_writable = true;
}

tcp_passive_session_routine*	tcp_passive_session_routine::create(routine_proxy *proxy, char category, int fd)
{
	if(-1 == fcntl(fd, F_SETFL, O_NONBLOCK))
	{
		proxy->get_log_api()->log(log_error, "create_tcp_passive_session_routine.fcntl fd=%d errno=%d",
				fd, errno);
		::close(fd);
		return	NULL;
	}
	
	set_flags(proxy, fd);

	proxy->get_log_api()->log(log_trace, "create_tcp_passive_session_routine,fd=%d", fd);
	return	new tcp_passive_session_routine(proxy, category, fd);
}

uint32_t	tcp_passive_session_routine::get_events()
{
	uint32_t	events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;

	if(!m_writable || m_send->data_len() > 0)
	{
		events |= EPOLLOUT;
	}
	return	events;
}

void	tcp_passive_session_routine::set_flags(routine_proxy *proxy, int fd)
{
	struct linger so_linger;
	so_linger.l_onoff = 0;
	so_linger.l_linger = 0;
	int enable = 1;
	int lr = setsockopt(fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
	if(lr != 0)
	{
		proxy->get_log_api()->log(log_error, "set fd=%d linger errno=%d", fd, errno);
	}
	int nr = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
	if(nr != 0)
	{
		proxy->get_log_api()->log(log_error, "set fd=%d nodelay errno=%d", fd, errno);
	}
}

tcp_passive_session_routine::~tcp_passive_session_routine()
{
}

int	tcp_passive_session_routine::on_readable()
{
	int result = do_nonblock_recv(*m_recv);
	if(result > 0)
	{
		return	service::get_instance()->process(m_proxy, this, m_recv);
	}
	else
	{
		service::get_instance()->process(m_proxy, this, m_recv);
		return	-1;
	}
}

int	tcp_passive_session_routine::on_writable()
{
	return	do_nonblock_write();
}

void	tcp_passive_session_routine::on_error()
{
	m_proxy->get_log_api()->log(this, log_info, "on_error,%d occurred", errno);

	if(ENETUNREACH == errno || ENETDOWN == errno || EADDRNOTAVAIL == errno)
	{
		/**
		 * EADDRNOTAVAIL:	The specified address is not available from the local machine.
		 * ENETUNREACH:		No route to the network is present.
		 * ENETDOWN:		The local network interface used to reach the destination is down.
		 */

		m_proxy->get_log_api()->log(this, log_error, "on_error,%d occurred", errno);
		close_fd();
	}
	else
	{
		m_proxy->get_log_api()->log(this, log_error, "on_error,%d occurred", errno);
		close_fd();
	}
}

int     tcp_passive_session_routine::inspect()
{
	return	0;
}

void	tcp_passive_session_routine::on_read_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_read_hup was triggered");
}

void	tcp_passive_session_routine::on_peer_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_peer_hup was triggered");
}
}
