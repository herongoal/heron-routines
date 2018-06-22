#include "routine.h"


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
signed long long	routine::s_unique_routine_id = 1;


routine::routine(routine_proxy *proxy, char category, uint64_t user_flag, int fd):
		m_proxy(proxy), m_routine_id(gen_routine_id()),
		m_category(category),
		m_writable(false),
		m_settled(false),
		m_fd(fd),
		m_send_times(0),
		m_recv_times(0),
		m_send_bytes(0),
		m_recv_bytes(0),
		m_user_data(NULL),
		m_user_flag(user_flag),
		m_last_inspect_time(0)
{
	m_create_time = m_proxy->current_utc_ms();
	m_touch_time = 0;

	m_recv = new circular_buffer(64 * 1024);
	m_send = new circular_buffer(64 * 1024);

	if(m_recv == NULL || m_send == NULL)
	{
		//m_proxy->get_log_api()->log(this, log_error, "circular_buffer::create failed");
		throw	std::bad_alloc();
	}

	memset(&m_peer_addr, 0, sizeof(m_peer_addr));
}

routine::~routine()
{
	m_proxy->get_log_api()->log(this, log_trace, "~routine");
	close_fd();
	delete	m_recv;
	delete	m_send;
}

int	routine::do_nonblock_write()
{
	unsigned bytes_sent = 0;
	int	result = do_nonblock_write(m_send->data_ptr(), m_send->data_len(), bytes_sent);
	if(result >= 0)
	{
		m_send->consume(bytes_sent);
		m_proxy->get_log_api()->log(this, log_trace, "do_nonblock_write %u bytes,%u not sent",
				bytes_sent, m_send->data_len());
	}

	return	result;
}

int	routine::do_nonblock_write(const void *buf, unsigned len, unsigned &sent)
{
	while(sent < len)
	{
		int ret = write(m_fd, (const char *)buf + sent, len - sent);
		
		++m_send_times;
		if(ret >= 0)
		{
			m_proxy->get_log_api()->log(this, log_all, "do_nonblock_write,%d bytes", ret);
			sent += ret;
			m_send_bytes += ret;
			continue;
		}

		const int err = errno;
		if(err == EWOULDBLOCK || err == EAGAIN)
		{
			m_writable = false;
			return	0;
		}
		else if(err == EINTR)
		{
			m_proxy->get_log_api()->log(this, log_all, "do_nonblock_write interrupted");
			continue;
		}
		else
		{
			m_proxy->get_log_api()->log(this, log_error, "do_nonblock_write errno=%d,errmsg=%s",
				err, strerror(err));
			return	-1;
		}
	}

	return	sent;
}

uint16_t	routine::get_proxy_id() const
{
	return  m_proxy->get_proxy_id();
}

int	routine::do_nonblock_recv(circular_buffer &cb)
{
	char		buff[64 * 1024];
	unsigned	max_recv = cb.unused_len();

	if(max_recv > sizeof(buff))
	{
		max_recv = sizeof(buff);
	}

	int len = read(m_fd, buff, max_recv);
	if(len > 0)
	{
		if(cb.append(buff, len))
		{
			m_proxy->get_log_api()->log(this, log_all, "do_nonblock_recv,%d bytes", len);
		}
		else
		{
			m_proxy->get_log_api()->log(this, log_error, "do_nonblock_recv,%d bytes save failed,buf.data_len=%u",
					len, cb.data_len());
		}
		++m_recv_times;
		m_recv_bytes += len;
		if(len == (int)max_recv)
		{
			return	0;
		}
		else
		{
			return	len;
		}
	}
	else if(len == 0)
	{
		m_proxy->get_log_api()->log(this, log_debug, "do_nonblock_recv,peer closed");
		return	-1;
	}
	else if(errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
	{
		m_proxy->get_log_api()->log(this, log_warn, "nothing was read");
		return	1;
	}
	else
	{
		m_proxy->get_log_api()->log(this, log_error, "read.errno=%d,strerror=%s",
				errno, strerror(errno));
		return	-1;
	}
}

void	routine::close()
{
	m_proxy->close_routine(m_routine_id);
}

void	routine::close_fd()
{
	if(m_fd >= 0)
	{
		m_proxy->get_log_api()->log(this, log_trace, "close_fd");
		m_proxy->unregister_events(this);
		::close(m_fd);
		m_fd = -1;
	}
}

bool	routine::append_send_data(const void *data, unsigned len)
{
	if(m_writable && m_send->data_len() == 0)
	{
		unsigned sent = 0;
		if(do_nonblock_write(data, len, sent) >= 0)
		{
			m_proxy->get_log_api()->log(this, log_all, "append_send_data.do_nonblock_write %u bytes", sent);

			if(sent < len)
			{
				m_writable = false;
				return	m_send->append((const char *)data + sent, len - sent);
			}
			return	true;
		}
		else
		{
			m_proxy->get_log_api()->log(this, log_error, "append_send_data.do_nonblock_write failed");
			return	false;
		}
	}

	if((m_send->append(data, len)))
	{
		m_proxy->get_log_api()->log(this, log_all, "append_send_data %u bytes success.", len);
		if(do_nonblock_write() >= 0)
		{
			if(m_send->data_len() > 0) m_writable = false;
			return	true;
		}
		else
		{
			return	false;
		}
	}
	else
	{
		m_proxy->get_log_api()->log(this, log_error, "append_send_data %u bytes failed!", len);
		return	false;
	}


	if(!m_writable)
	{
		m_proxy->modify_events(this);
	}
}
}
