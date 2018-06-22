#include "ipc_routine.h"


#include "circular_buffer.h"
#include "log_api.h"
#include "routine_proxy.h"
#include "service_define.h"
#include "udp_session_context.h"
#include "service.h"


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ipc.h>
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
ipc_routine::ipc_routine(routine_proxy *proxy, uint16_t routine_type, int fd):
	routine(proxy, routine_type, 0, fd)
{
	m_writable = true;
}

ipc_routine::~ipc_routine()
{
	cout << "~ipc_routine" << m_routine_id << endl;
}

bool    ipc_routine::settle_routine(uint8_t dest_proxy, routine *rt)
{       
	ipc_msg_header  header(ipc_msg_settle_routine);

	header.src_proxy_id = m_proxy->get_proxy_id();
	header.src_routine_id = rt->get_routine_id();
	header.dst_routine_id = header.src_routine_id;
	header.dst_proxy_id = dest_proxy;

	memcpy(header.data, rt, sizeof(header.data));

	string  s((const char *)&header, sizeof(header));
	if(m_send_list.empty() && m_writable)
	{
		unsigned bytes_sent = 0;
		int ret = do_nonblock_write(s.c_str(), s.size(), bytes_sent);
		if(ret == (int)s.size())
		{
			return	true;
		}
		else if(ret < 0)
		{
			return	false;
		}
		else
		{
			m_send_list.push_back(s);
			return	true;
		}
	}

	m_send_list.push_back(s);
	return	true;
}

bool    ipc_routine::transfer_data(uint8_t dst_proxy, const void *data, const unsigned data_len)
{
		ipc_msg_header  header(ipc_msg_transfer_data);
        header.dst_routine_id = 0u;
        header.dst_proxy_id = dst_proxy;
        string  s((const char *)&header, sizeof(header));
        s.append((const char *)data, data_len);


	if(m_writable)
	{
		unsigned bytes_sent = 0;
		int ret = do_nonblock_write(s.c_str(), s.size(), bytes_sent);
		if(ret == (int)s.size())
		{
			return  true;
		}
		else if(ret < 0)
		{
			return  false;
		}
	}

	m_writable = false;
	m_proxy->modify_events(this);
	m_send_list.push_back(s);
	return	true;
}

bool    ipc_routine::send_msg(uint8_t dst_proxy, uint64_t dst_routine,
		const void *data,
		const unsigned data_len)
{
	ipc_msg_header  header(ipc_msg_send_msg);
        header.dst_routine_id = dst_routine;
        header.dst_proxy_id = dst_proxy;
        string  s((const char *)&header, sizeof(header));
        s.append((const char *)data, data_len);


	if(m_writable)
	{
		unsigned bytes_sent = 0;
		int ret = do_nonblock_write(s.c_str(), s.size(), bytes_sent);
		if(ret == (int)s.size())
		{
			return  true;
		}
		else if(ret < 0)
		{
			return  false;
		}
	}

	m_writable = false;
	m_proxy->modify_events(this);
	m_send_list.push_back(s);
	return	true;
}

bool    ipc_routine::append_send_data(const void *data, const unsigned len)
{
	return  true;
}

bool    ipc_routine::notify_stopped()
{
	ipc_msg_header  header(ipc_msg_notify_stopped);
	header.src_proxy_id = m_proxy->get_proxy_id();
	header.dst_proxy_id = 0;
	header.src_routine_id = 0;
	header.dst_routine_id = 0;

	string  s((const char *)&header, sizeof(header));

	if(m_writable)
        {
                unsigned bytes_sent = 0;
                int ret = do_nonblock_write(s.c_str(), s.size(), bytes_sent);
                if(ret == (int)s.size())
                {
                        return  true;
                }
                else if(ret < 0)
                {
                        return  false;
                }
                else
                {
                        m_send_list.push_back(s);
                        return  true;
                }
        }

	m_send_list.push_back(s);
	return  true;
}

bool    ipc_routine::send_stop()
{
	ipc_msg_header  header(ipc_msg_stop_prx);
	header.dst_routine_id = 0;
	header.dst_proxy_id = 0;
	string  s((const char *)&header, sizeof(header));


	if(m_writable)
        {
                unsigned bytes_sent = 0;
                int ret = do_nonblock_write(s.c_str(), s.size(), bytes_sent);
                if(ret == (int)s.size())
                {
			cout << "send_stop:" << endl;
                        return  true;
                }
                else if(ret < 0)
                {
                        return  false;
                }
        }

	m_writable = false;
	m_proxy->modify_events(this);
	m_send_list.push_back(s);
	return  true;
}

uint32_t	ipc_routine::get_events()
{
	uint32_t	events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;

	if(!m_writable || m_send_list.size() > 0)
	{
		events |= EPOLLOUT;
	}
	return	events;
}

int	ipc_routine::on_readable()
{
	const	int	buff_size = 64 * 1024;
	char	buff[buff_size];
	int		bytes_read = 0;

	for(int read_sum = 0; read_sum < buff_size; read_sum += bytes_read)
	{
		bytes_read = read(m_fd, buff, sizeof(buff));
		if(0 == bytes_read)
		{
			continue;
		}
		else if(0 > bytes_read)
		{
			break;
		}

		ipc_msg_header *imh = (ipc_msg_header *)buff;
		unsigned data_len = bytes_read - sizeof(ipc_msg_header) + sizeof(imh->data);

		if(imh->msg_type == ipc_msg_send_msg)
		{
			m_proxy->send_message(imh->dst_proxy_id, imh->dst_routine_id, imh->data, data_len);
		}
		else if(imh->msg_type == ipc_msg_stop_prx)
		{
			m_proxy->require_stop();
		}
		else if(imh->msg_type == ipc_msg_transfer_data)
		{
			/*
			udp_session_context *usc = (udp_session_context *)m_proxy->m_routine_mgr.search_elem();
			if(NULL != usc)
			{
				usc->dispose(imh->data, data_len);
			}
			else
			{
				cout << "" << endl;
			}
			*/
		}
		else if(imh->msg_type == ipc_msg_settle_routine)
		{
			routine *rt = NULL;
			memcpy(rt, imh->data, sizeof(imh->data));
			rt->m_settled = true;
			m_proxy->add_routine(rt);
		}
		else if(imh->msg_type == ipc_msg_notify_stopped)
		{
			service::get_instance()->proc_proxy_stop(imh->src_proxy_id);
		}
		else
		{
			m_proxy->get_log_api()->log(this, log_error, "unexpected ipc msg type");
		}
	}

	return	0;
}

int	ipc_routine::on_writable()
{
	list<string>::iterator iter = m_send_list.begin();
	while(iter != m_send_list.end())
	{
		const string &str = *iter;
		unsigned bytes_sent = 0;
		int ret = do_nonblock_write(str.c_str(), str.size(), bytes_sent);
		if(ret == (int)str.size())
		{
			iter = m_send_list.erase(iter);
		}
		else
		{
			break;
		}
	}
	return	0;
}

void	ipc_routine::on_error()
{
	m_proxy->get_log_api()->log(this, log_error, "on_error was triggered");
}

void	ipc_routine::on_read_hup()
{
	m_proxy->get_log_api()->log(this, log_error, "ipc_routine.on_read_hup was triggered");
}

void	ipc_routine::on_peer_hup()
{
	m_proxy->get_log_api()->log(this, log_error, "ipc_routine.on_peer_hup was triggered");
}
}
