#include "log_routine.h"


#include "circular_buffer.h"
#include "log_api.h"
#include "routine_proxy.h"
#include "service_define.h"
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
log_routine    *log_routine::create(routine_proxy *proxy, char category, int fd)
{
        log_routine *nr = new log_routine(proxy, category, fd);
        if(NULL == nr)
        {
                REPORT_EVENT("create log_routine fd=%d for proxy %d failed!",
			fd, proxy->get_proxy_id());
                ::close(fd);
        }
        else
        {
                REPORT_EVENT("create log_routine fd=%d for proxy %d finished.",
			fd, proxy->get_proxy_id());
        }

        return    nr;
}

log_routine::log_routine(routine_proxy *proxy, char category, int fd):
        routine(proxy, category, 0, fd)
{
        m_writable = true;
}

log_routine::~log_routine()
{
}

bool    log_routine::append_send_data(const void *data, const unsigned data_len)
{
        ipc_msg_header  header(ipc_msg_send_log);
        header.dst_routine_id = 0;
        header.dst_proxy_id = 0;
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
        m_send_list.push_back(s);
	m_proxy->modify_events(this);
        return  true;
}

int   	log_routine::inspect()
{
	return	0;
}


uint32_t    log_routine::get_events()
{
        uint32_t    events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;

        if(!m_writable || m_send_list.size() > 0)
        {
                events |= EPOLLOUT;
        }
        return    events;
}

int    log_routine::on_readable()
{
        char    buff[65535];
        while(true)
        {
                int    bytes_read = read(m_fd, buff, sizeof(buff));
                if(bytes_read <= 0)
                {
                        break;
                }

		ipc_msg_header	imh(ipc_msg_send_log);
		memcpy(&imh, buff, sizeof(imh));

                if(imh.msg_type == ipc_msg_send_log)
                {
                        m_proxy->send_log(buff + sizeof(imh), bytes_read - sizeof(imh));
                }
                else
                {
			REPORT_EVENT("log_routine.on_readable,bad message received");
                }
        }

        return    0;
}

int    log_routine::on_writable()
{
	m_writable = true;
        list<string>::iterator iter = m_send_list.begin();
        while(iter != m_send_list.end())
        {
                string &str = *iter;
                unsigned bytes_sent = 0;
                int ret = do_nonblock_write(str.c_str(), str.size(), bytes_sent);
                if(ret == (int)str.size())
                {
                        iter = m_send_list.erase(iter);
                }
		else if(ret == 0)
		{
			m_writable = false;
			m_proxy->modify_events(this);
			return	0;
		}
                else
                {
                        break;
                }
        }
        return    0;
}

void    log_routine::on_error()
{
        m_proxy->get_log_api()->log(this, log_error, "on_error was triggered");
}

void    log_routine::on_read_hup()
{
        m_proxy->get_log_api()->log(this, log_error, "log_routine.on_read_hup was triggered");
}

void    log_routine::on_peer_hup()
{
        m_proxy->get_log_api()->log(this, log_error, "log_routine.on_peer_hup was triggered");
}
}
