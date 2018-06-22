#include "udp_service_routine.h"


#include "circular_buffer.h"
#include "log_api.h"
#include "routine_proxy.h"
#include "udp_session_context.h"
#include "service.h"


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/select.h>
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
const   int     udp_service_routine::m_socket_buffer_size = 1024 * 1024;
udp_service_routine	*udp_service_routine::create(routine_proxy *proxy, char category, uint64_t user_flag,
		const char *ipaddr, uint16_t port)
{
	struct sockaddr_in  svr_addr = {AF_INET, htons(port), {0u}, 
			{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

	log_api *logger = proxy->get_log_api();

	if(0 == inet_aton(ipaddr, &svr_addr.sin_addr))
	{
		logger->log(log_error, "udp_service_routine.create invalid ipaddr=%s", ipaddr);
		return	NULL;
	}

	int	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd < 0)
	{
		logger->log(log_error, "udp_service_routine.socket errno=%d,errmsg=%s",
				errno, strerror(errno));
		return	NULL;
	}

	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &m_socket_buffer_size, sizeof(m_socket_buffer_size));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &m_socket_buffer_size, sizeof(m_socket_buffer_size));

	do{
		if(-1 == fcntl(fd, F_SETFL, O_NONBLOCK))
		{
			logger->log(log_error, "udp_service_routine.fcntl errno=%d,errmsg=%s",
					errno, strerror(errno));
			break;
		}

		if(0 != bind(fd, (struct sockaddr *)&svr_addr, sizeof(svr_addr)))
		{
			logger->log(log_error, "udp_service_routine.bind errno=%d,errmsg=%s",
					errno, strerror(errno));
			break;
		}

		udp_service_routine *sr = new udp_service_routine(proxy, category, user_flag, fd);
		if(NULL == sr)
		{
			logger->log(log_error, "udp_service_routine.create bad_alloc,close fd=%d", fd);
			break;
		}
		return	sr;
	}while(false);

	::close(fd);
	return	NULL;
}

udp_service_routine::udp_service_routine(routine_proxy *proxy, char category, uint64_t user_flag, int fd):
	routine(proxy, category, user_flag, fd)
{
	m_proxy->get_log_api()->log(this, log_all, "udp_service_routine");
	m_writable = false;
}

udp_service_routine::~udp_service_routine()
{
	m_proxy->get_log_api()->log(this, log_all, "~udp_service_routine");
}

uint32_t	udp_service_routine::get_events()
{
	return	EPOLLIN;
}

int	udp_service_routine::on_readable()
{
	static	const	int	min_pack_len = sizeof(keep_alive_msg);
	static	const	int	flag = MSG_DONTWAIT;
	struct	sockaddr_in	cli_addr;

	char	data[65535];
	int		bytes_read = 0;

	int	count = 0;

	while(bytes_read < m_socket_buffer_size)
	{
		socklen_t	cli_len = sizeof(cli_addr);
		int recv_len = recvfrom(m_fd, data, sizeof(data), flag, (struct sockaddr*)&cli_addr, &cli_len);

		if(-1 == recv_len)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			{
				break;
			}
			else
			{
				m_proxy->get_log_api()->log(this, log_error, "on_readable.recvfrom, %d occurred", errno);
				return	-1;
			}
			return	-1;
		}

		bytes_read += recv_len;
		if(recv_len < min_pack_len)
		{
			m_proxy->get_log_api()->log(this, log_trace, "on_readable.recv_len=%u", min_pack_len);
			continue;
		}
		
		++count;

		fragment_header header;
		unsigned pos = 0;
		header.read_conversation(data, recv_len, pos);
		header.read_frag_type(data, recv_len, pos);

		udp_session_context *rt = (udp_session_context *)m_proxy->get_routine(header.conversation);

		if(header.frag_type == fragment_type_set_session)
		{
			//in fact the field read here(ts) is the conv of client
			header.read_ts(data, recv_len, pos);
			if(NULL != rt && header.ts != rt->m_session_conv)
			{
				int64_t routine_id = rt->m_conversation_id;
				rt->m_fd = -1;
				service::get_instance()->on_routine_closed(rt);
				m_proxy->close_routine(routine_id);
			}
			rt = udp_session_context::create(m_proxy, m_category, m_fd);
			if(NULL != rt)
			{
				rt->m_routine_id = header.conversation;
				rt->m_conversation_id = header.conversation;
				rt->m_session_conv = header.ts;
				m_proxy->add_routine(rt);
			}
			else
			{
				m_proxy->get_log_api()->log(this, log_fatal, "create routine=%lu failed",
					header.conversation);
			}
		}
		
		if(NULL == rt)
		{
			m_proxy->get_log_api()->log(this, log_trace, "dropped data for routine=%lu",
				header.conversation);
			continue;
		}

		memcpy(&rt->m_peer_addr, &cli_addr, sizeof(cli_addr));
		if(0 != rt->dispose(data, recv_len))
		{
			m_proxy->get_log_api()->log(rt, log_error, "dispose %u bytes error occurred.", recv_len);
			rt->m_fd = -1;
			service::get_instance()->on_routine_closed(rt);
			m_proxy->close_routine(rt->m_routine_id);
		}
	}

	m_proxy->get_log_api()->log(this, log_trace, "proc count=%d", count);
	return	0;
}

int	udp_service_routine::on_writable()
{
	return	0;
}

void	udp_service_routine::on_error()
{
	m_proxy->get_log_api()->log(this, log_error, "on_error was triggered");
	close_fd();
}

void	udp_service_routine::on_read_hup()
{
	m_proxy->get_log_api()->log(this, log_error, "on_read_hup was triggered");
}

void	udp_service_routine::on_peer_hup()
{
	m_proxy->get_log_api()->log(this, log_error, "on_peer_hup was triggered");
}

int     udp_service_routine::inspect()
{
	return	0;
}
}
