#include "tcp_service_routine.h"


#include "circular_buffer.h"
#include "log_api.h"
#include "routine_proxy.h"
#include "tcp_passive_session_routine.h"
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
const int	tcp_service_routine::s_reuse_port = 1;
tcp_service_routine	*tcp_service_routine::create(routine_proxy *proxy, char category,
		uint64_t routine_flag,
		const char *ipaddr, uint16_t port)
{
	struct sockaddr_in  addr = {AF_INET, htons(port), {0u}, 
			{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

	log_api *logger = proxy->get_log_api();

	if(0 == inet_aton(ipaddr, &addr.sin_addr))
	{
		logger->log(log_error, "tcp_service_routine.inet_aton error,ipaddr=%s",
				errno, strerror(errno));
		return	NULL;
	}

	int	fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd < 0)
	{
		logger->log(log_error, "tcp_service_routine.socket errno=%d,errmsg=%s",
				errno, strerror(errno));
		return	NULL;
	}

	if(-1 == fcntl(fd, F_SETFL, O_NONBLOCK))
	{
		logger->log(log_error, "tcp_service_routine.fcntl errno=%d,errmsg=%s",
				errno, strerror(errno));
		::close(fd);
		return	NULL;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &s_reuse_port, sizeof(s_reuse_port));
	if(0 != bind(fd, (struct sockaddr *)&addr, sizeof(addr)))
	{
		logger->log(log_error, "tcp_service_routine.bind errno=%d,errmsg=%s",
				errno, strerror(errno));
		::close(fd);
		return	NULL;
	}

	if(0 != listen(fd, 2000))
	{
		logger->log(log_error, "tcp_service_routine.listen errno=%d,errmsg=%s",
				errno, strerror(errno));
		::close(fd);
		return	NULL;
	}
	tcp_service_routine *sr = new tcp_service_routine(proxy, category, routine_flag, fd);
	if(NULL == sr)
	{
		logger->log(log_error, "tcp_service_routine.create bad_alloc,close fd=%d", fd);
		::close(fd);
	}
	return	sr;
}

tcp_service_routine::tcp_service_routine(routine_proxy *proxy, char category, uint64_t user_flag, int fd):
	routine(proxy, category, user_flag, fd)
{
	m_proxy->get_log_api()->log(this, log_all, "tcp_service_routine");
	m_writable = false;
}

tcp_service_routine::~tcp_service_routine()
{
	m_proxy->get_log_api()->log(this, log_all, "~tcp_service_routine");
}

uint32_t	tcp_service_routine::get_events()
{
	return	EPOLLIN;
}

int	tcp_service_routine::on_readable()
{
	while(true)
	{
		socklen_t	len = sizeof(struct	sockaddr_in);
		struct	sockaddr_in	addr;

		int conn_fd = accept(m_fd, (struct sockaddr *)&addr, &len);
		if(conn_fd >= 0)
		{
			tcp_passive_session_routine *pr = tcp_passive_session_routine::create(m_proxy, m_category, conn_fd);
			if(pr == NULL)
			{
				m_proxy->get_log_api()->log(this, log_fatal, "on_readable.accept bad_alloc for conn_fd=%d", conn_fd);
				::close(conn_fd);
			}
			else
			{
				m_proxy->get_log_api()->log(this, log_trace, "on_readable.accept conn_fd=%d", conn_fd);
				m_proxy->add_routine(pr);
			}
			continue;
		}

		if(errno == EAGAIN)
		{
			m_proxy->get_log_api()->log(this, log_all, "on_readable.accept, all conn accepted");
			break;
		}
		else
		{
			m_proxy->get_log_api()->log(this, log_error, "on_readable.accept, %d occurred", errno);
		}
	}
	return	0;
}

int	tcp_service_routine::on_writable()
{
	m_proxy->get_log_api()->log(this, log_error, "on_writable was triggered");
	return	0;
}

void	tcp_service_routine::on_error()
{
	m_proxy->get_log_api()->log(this, log_error, "on_error was triggered");
	close_fd();
}

void	tcp_service_routine::on_read_hup()
{
	m_proxy->get_log_api()->log(this, log_error, "on_read_hup was triggered");
}

void	tcp_service_routine::on_peer_hup()
{
	m_proxy->get_log_api()->log(this, log_error, "on_peer_hup was triggered");
}
}
