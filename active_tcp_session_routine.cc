#include "active_tcp_session_routine.h"
#include "circular_buffer.h"
#include "log_api.h"
#include "routine_proxy.h"
#include "service.h"


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
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
active_session_routine::active_session_routine(routine_proxy *proxy, uint64_t user_flag):
	routine(proxy, 0, user_flag, -1), m_established(false)
{
	m_writable = false;
}

active_session_routine::~active_session_routine()
{
}

uint32_t	active_session_routine::get_events()
{
		uint32_t	events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
		
		if(!m_writable || m_send->data_len() > 0)
		{
			events |= EPOLLOUT;
		}
		return	events;
}

int	active_session_routine::on_readable()
{
	m_proxy->get_log_api()->log(this, log_all, "active_session_routine.on_readable");

	int result = do_nonblock_recv(*m_recv);
	if(result >= 0)
	{
		string s;
		const char *ptr = (const char *)m_recv->data_ptr();
		for(size_t n = 0; n < m_recv->data_len(); ++n)
		{
			const char digits[] = "0123456789abcdef";
			unsigned char c = ptr[n];
			char h = digits[c / 16];
			char l = digits[c % 16];
			s.append(1, h);
			s.append(1, l);
		}

		return	service::get_instance()->process(m_proxy, this, m_recv);
	}
	else
	{
		service::get_instance()->process(m_proxy, this, m_recv);
		return  -1;
	}
}

int	active_session_routine::on_writable()
{
	m_writable = true;
	if(!m_established)
	{
		service::get_instance()->create_routine_proc(m_user_flag, m_routine_id);
		m_proxy->get_log_api()->log(this, log_trace, "on_writable was triggered");
		m_established = true;
	}
	return	do_nonblock_write();
}

void	active_session_routine::on_error()
{
	if(!m_writable)
	{
		/**
		 *	ECONNRESET:		Remote host reset the connection request.
		 *
		 *	EHOSTUNREACH:	The destination host cannot be reached (probably 
		 *	because the host is down or a remote router cannot reach it).
		 *
		 *	ECONNREFUSED:	The target address was not listening for
		 *	connections or refused the connection request.
		 */

		if(ECONNRESET == errno || EHOSTUNREACH == errno || ECONNREFUSED == errno)
		{
			m_proxy->get_log_api()->log(this, log_error, "on_error,%d occurred", errno);
			close_fd();
		}
		else
		{
			m_proxy->get_log_api()->log(this, log_error, "on_error,%d occurred", errno);
			close_fd();
		}
	}
	else if(ENETUNREACH == errno || ENETDOWN == errno || EADDRNOTAVAIL == errno)
	{
		/**
		 * EADDRNOTAVAIL:	The specified address is not available from the local machine.
		 * ENETUNREACH:		No route to the network is present.
		 * ENETDOWN:			The local network interface used to reach the destination is down.
		 */
		m_proxy->get_log_api()->log(this, log_error, "in event_process occurred,rpc_state=rpc_in_isolated");
		close_fd();
	}
	else
	{
		m_proxy->get_log_api()->log(this, log_error, "occurred,rpc_state=rpc_recv_failed");
		close_fd();
	}
}

void	active_session_routine::check_conn_state(int err)
{
	/**
	 * Although POSIX 
	 */
	if(EISCONN == err || EALREADY == err || EINPROGRESS == err)
	{
		/**
		 *      EALREADY:       A connection request is already in progress for the specified socket.
		 *      EISCONN:        The specified socket is connection-mode and is already connected.
		 */
		m_proxy->get_log_api()->log(this, log_debug, "active_session_routine.check_conn_state, errno=%d", err);
	}
	else if(ECONNRESET == err || EHOSTUNREACH == err || ECONNREFUSED == err)
	{
		/**
		 *      ECONNRESET:             Remote host reset the connection request.
		 *
		 *      EHOSTUNREACH:   The destination host cannot be reached (probably 
		 *      because the host is down or a remote router cannot reach it).
		 *
		 *      ECONNREFUSED:   The target address was not listening for
		 *      connections or refused the connection request.
		 */
		m_proxy->get_log_api()->log(this, log_error, "active_session_routine.check_conn_state, errno=%d", err);
		close_fd();
		return  ;
	}
	else if(ENETUNREACH == err || ENETDOWN == err || EADDRNOTAVAIL == err)
	{
		/**
		 * EADDRNOTAVAIL:       The specified address is not available from the local machine.
		 * ENETUNREACH:         No route to the network is present.
		 * ENETDOWN:            The local network interface used to reach the destination is down.
		 */
		m_proxy->get_log_api()->log(this, log_error, "active_session_routine.check_conn_state, errno=%d", err);
		close_fd();
		return  ;
	}
	else if(EINTR == errno)
	{
		/**
		 * The attempt to establish a connection was interrupted by delivery of a 
		 * signal that was caught; the connection shall be established asynchronously.
		 * refer to http://www.madore.org/~david/computers/connect-intr.html
		 * for some useful information about the EINTR after connect
		 */
		m_proxy->get_log_api()->log(this, log_debug, "active_session_routine,EINTR");
	}
	else
	{
		m_proxy->get_log_api()->log(this, log_error, "active_session_routine.check_conn_state, errno=%d", err);
		close_fd();
		return  ;
	}
}

void	active_session_routine::on_read_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_read_hup was triggered");
}

void	active_session_routine::on_peer_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_peer_hup was triggered");
}



active_session_routine*	active_session_routine::create(routine_proxy *proxy, uint64_t user_flag,
		const char *ipaddr, uint16_t port)
{
	active_session_routine *sr = new (std::nothrow)active_session_routine(proxy, user_flag);

	if(sr == NULL)
	{
		proxy->get_log_api()->log(log_error, "active_session_routine.create bad_alloc");
		return	sr;
	}

	sr->m_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(sr->m_fd < 0 || -1 == fcntl(sr->m_fd, F_SETFL, O_NONBLOCK))
	{
		proxy->get_log_api()->log(log_error, "active_session_routine.create,%d occurred",
				errno);
		delete	sr;
		return	NULL;
	}

	bzero(&sr->m_peer_addr, sizeof(sr->m_peer_addr));
	sr->m_peer_addr.sin_family = AF_INET;
	sr->m_peer_addr.sin_port = htons(port);
	inet_aton(ipaddr, &sr->m_peer_addr.sin_addr);

	sr->m_writable = false;
	/**
	 * circumstance that connection is established immediately after calling connect non-blockly
	 * especially when client and server are on the same machine do exist.
	 */
	if(connect(sr->m_fd, (struct sockaddr *)&sr->m_peer_addr, sizeof(sr->m_peer_addr)) == 0)
	{
		sr->m_writable = true;
	}

	proxy->get_log_api()->log(log_trace, "active_session_routine.create,m_writable=%d",
			(int)sr->m_writable);
	return	sr;
}

int     active_session_routine::inspect()
{
	return	0;
}
}//end of namespace hrts
