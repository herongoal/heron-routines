#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <errno.h>


#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdarg.h>
#include <signal.h>
#include <string>


#include "service.h"
#include "service_define.h"
#include "tcp_service_routine.h"
#include "udp_service_routine.h"
#include "tcp_passive_session_routine.h"
#include "active_tcp_session_routine.h"
#include "active_udp_session_routine.h"
#include "routine.h"
#include "routine_proxy.h"
#include "circular_buffer.h"


namespace	hrts{
using namespace std;
service*	service::m_service_instance = NULL;

service::service(const string &log_file, size_t slice_kb, log_level level, unsigned proxy_count):
	m_log_level(level),
	m_proxy_exit_count(0),
	m_routine_proxy_count(proxy_count),
	m_log_proxy_id(0),
	m_ipc_proxy_id(sizeof(m_proxy_array) / sizeof(m_proxy_array[0]) - 1),
	m_service_state(service_state_init)
{
	bzero(m_proxy_array, sizeof(m_proxy_array));
	bzero(m_log_routine, sizeof(m_log_routine));
	bzero(m_ipc_routine, sizeof(m_ipc_routine));

	const	int max = sizeof(m_proxy_array) / sizeof(m_proxy_array[0]);
	if(m_routine_proxy_count > max - 2) m_routine_proxy_count = max - 2;

	file_writer* log_writer = new file_writer(log_file, slice_kb);
	m_proxy_array[m_log_proxy_id] = new routine_proxy(m_log_proxy_id, true, level, log_writer);

	m_proxy_array[m_ipc_proxy_id] = new routine_proxy(m_ipc_proxy_id, false, level, NULL);
	m_routine_proxy_map[pthread_self()] = m_ipc_proxy_id;

	log_routine *lrt = log_routine::create(m_proxy_array[m_log_proxy_id], 'l', m_proxy_array[m_ipc_proxy_id]->m_log_sockpair_fd[1]);
	m_log_routine[m_ipc_proxy_id] = lrt;
	m_proxy_array[m_log_proxy_id]->add_routine(lrt);

	for(size_t proxy_id = 1; proxy_id <= proxy_count; ++proxy_id)
	{
		routine_proxy *rtp = new routine_proxy(proxy_id, true, level, NULL);
		m_proxy_array[proxy_id] = rtp;
		if(NULL == rtp)
		{
			REPORT_EVENT("create routine_proxy=%d failed", proxy_id);
			return  ;
		}

		log_routine *lrt = log_routine::create(m_proxy_array[m_log_proxy_id], 'l', rtp->m_log_sockpair_fd[1]);
		m_log_routine[proxy_id] = lrt;
		m_proxy_array[m_log_proxy_id]->add_routine(lrt);

		ipc_routine *irt = new (std::nothrow)ipc_routine(m_proxy_array[m_ipc_proxy_id], 'c', rtp->m_ipc_sockpair_fd[1]);
		m_ipc_routine[proxy_id] = irt;
		m_proxy_array[m_ipc_proxy_id]->add_routine(irt);
	}

	m_service_instance = this;
	g_big_edian = test_byte_order();
}

service::~service()
{
	for(size_t n = 0; n < sizeof(m_proxy_array) / sizeof(m_proxy_array[0]); ++n)
	{
		if(NULL != m_proxy_array[n])
		{
			delete	m_proxy_array[n];
			m_proxy_array[n] = NULL;
		}
	}
}

void    service::info_log(const char *format, ...)
{
	if(log_info <= m_log_level)
	{
		pthread_t       thread_id = pthread_self();
		hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
		if(pos != m_routine_proxy_map.end())
		{
			const int8_t    proxy_id = pos->second;

			char    msg[4096] = { 0 };
			va_list ap;
			va_start(ap, format);
			int msg_len = vsnprintf(msg, sizeof(msg), format, ap);
			va_end(ap);
			if(msg_len >= (int)sizeof(msg))
				msg_len = sizeof(msg) - 1;
			msg[msg_len++] = '\n';
			m_proxy_array[proxy_id]->get_log_api()->log(__FILE__, __LINE__, log_info, msg_len, msg);
		}
		else
		{
			REPORT_EVENT("info_log from unknown thread_id=%lu refused", thread_id);
		}
	}
}

void    service::warn_log(const char *format, ...)
{
        if(log_warn <= m_log_level)
        {
                pthread_t       thread_id = pthread_self();
                hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
                if(pos != m_routine_proxy_map.end())
                {
                        const int8_t    proxy_id = pos->second;

                        char    msg[4096] = { 0 };
                        va_list ap;
                        va_start(ap, format);
                        int msg_len = vsnprintf(msg, sizeof(msg), format, ap);
                        va_end(ap);
                        if(msg_len >= (int)sizeof(msg))
                                msg_len = sizeof(msg) - 1;
                        msg[msg_len++] = '\n';
                        m_proxy_array[proxy_id]->get_log_api()->log(__FILE__, __LINE__, log_warn, msg_len, msg);
                }
                else
                {
                        REPORT_EVENT("warn_log from unknown thread_id=%lu refused", thread_id);
                }
        }
}

void    service::fatal_log(const char *format, ...)
{
        if(log_fatal <= m_log_level)
        {
                pthread_t       thread_id = pthread_self();
                hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
                if(pos != m_routine_proxy_map.end())
                {
                        const int8_t    proxy_id = pos->second;

                        char    msg[4096] = { 0 };
                        va_list ap;
                        va_start(ap, format);
                        int msg_len = vsnprintf(msg, sizeof(msg), format, ap);
                        va_end(ap);
                        if(msg_len >= (int)sizeof(msg))
                                msg_len = sizeof(msg) - 1;
                        msg[msg_len++] = '\n';
                        m_proxy_array[proxy_id]->get_log_api()->log(__FILE__, __LINE__, log_fatal, msg_len, msg);
                }
                else
                {
                        REPORT_EVENT("fatal_log from unknown thread_id=%lu refused", thread_id);
                }
        }
}

void    service::error_log(const char *format, ...)
{
	if(log_error <= m_log_level)
	{
		pthread_t       thread_id = pthread_self();
		hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
		if(pos != m_routine_proxy_map.end())
		{
			const int8_t    proxy_id = pos->second;

			char    msg[4096] = { 0 };
			va_list ap;
			va_start(ap, format);
			int msg_len = vsnprintf(msg, sizeof(msg), format, ap);
			va_end(ap);
			if(msg_len >= (int)sizeof(msg))
				msg_len = sizeof(msg) - 1;
			msg[msg_len++] = '\n';
			m_proxy_array[proxy_id]->get_log_api()->log(__FILE__, __LINE__, log_error, msg_len, msg);
		}
		else
		{
			REPORT_EVENT("error_log from unknown thread_id=%lu refused", thread_id);
		}
	}
}

void    service::debug_log(const char *format, ...)
{
	if(log_debug <= m_log_level)
	{
		pthread_t       thread_id = pthread_self();
		hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
		if(pos != m_routine_proxy_map.end())
		{
			const int8_t    proxy_id = pos->second;
                        
                        char    msg[4096] = { 0 };
                        va_list ap;
                        va_start(ap, format);
                        int msg_len = vsnprintf(msg, sizeof(msg), format, ap);
                        va_end(ap);
			if(msg_len >= (int)sizeof(msg))
			msg_len = sizeof(msg) - 1;
			msg[msg_len++] = '\n';
                        m_proxy_array[proxy_id]->get_log_api()->log(__FILE__, __LINE__, log_debug, msg_len, msg);
		}
		else
		{
                        REPORT_EVENT("debug_log from unknown thread_id=%lu refused", thread_id);
		}
	}
}

void    service::trace_log(const char *format, ...)
{
	if(log_trace <= m_log_level)
	{
		pthread_t       thread_id = pthread_self();
                hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
		if(pos != m_routine_proxy_map.end())
		{
			const int8_t    proxy_id = pos->second;
                        
                        char    msg[4096] = { 0 };
                        va_list ap;
                        va_start(ap, format);
                        int msg_len = vsnprintf(msg, sizeof(msg), format, ap);
                        va_end(ap);
			if(msg_len >= (int)sizeof(msg))
			msg_len = sizeof(msg) - 1;
			msg[msg_len++] = '\n';
                        m_proxy_array[proxy_id]->get_log_api()->log(__FILE__, __LINE__, log_trace, msg_len, msg);
		}
		else
		{
                        REPORT_EVENT("trace_log from unknown thread_id=%lu refused", thread_id);
		}
	}
}

int	service::process(routine_proxy *proxy, routine *r, circular_buffer *buf)
{
	static const size_t	header_len = sizeof(service_msg_header);
	proxy->get_log_api()->log(r, log_trace, "buf_len=%u", buf->data_len());
	while(buf->data_len() >= header_len)
	{
		service_msg_header	header;
		memcpy(&header, buf->data_ptr(), sizeof(header));

		if(header.msg_size > buf->data_len())
		{
			proxy->get_log_api()->log(r, log_trace, "incomplete,msg_size=%u,msg_type=%u,reserved=%u,sequence=%u,data_len=%u,recv_bytes=%u,recv_times=%u",
					header.msg_size, header.msg_type,
					header.reserved, header.sequence,
					buf->data_len(), r->m_recv_bytes, r->m_recv_times);
			return	0;
		}
		else if(header.msg_size < header_len)
		{
			proxy->get_log_api()->log(r, log_error, "bad msg received");
			return	-1;
		}
		else
		{
			int ret = process(proxy->get_log_api(), r, header, (const char *)buf->data_ptr() + header_len, header.msg_size - header_len);
			if(ret == 0)
			{
				proxy->get_log_api()->log(r, log_trace, "process,msg_size=%u,msg_type=%u,reserved=%u,sequence=%u,data_len=%u\n",
						header.msg_size, header.msg_type,
						header.reserved, header.sequence,
						buf->data_len());


				if(!buf->consume(header.msg_size))
				{
				}
			}
			else if(ret > 0)
			{
				//routine settled, nothing else to do.
				cout << "settle r=" << r->m_routine_id << endl;
				break;
			}
			else
			{
				proxy->get_log_api()->log(r, log_error, "process error");
			}
		}
	}

	return	0;
}

void    service::create_routine_proc(uint64_t user_flag, uint64_t routine_id)
{
	m_channel_routines[user_flag] = routine_id;
	on_routine_created(user_flag, routine_id);
}

void    service::signal_handle(int sigid, siginfo_t *si, void *unused)
{
	service::get_instance()->info_log("received signal=%d", sigid);

        if(SIGTERM == sigid || SIGINT == sigid)
        {
		service::get_instance()->info_log("signal.action stop_service");
		service::get_instance()->m_service_state = service_state_stop;
        }
        else if(SIGPIPE == sigid || SIGHUP == sigid)
        {
		service::get_instance()->info_log("signal.action ignore");
        }
        else if(SIGUSR1 == sigid)
        {
		service::get_instance()->info_log("signal.action reload config");
		service::get_instance()->reload_config();
        }
        else if(SIGUSR2 == sigid)
        {
		service::get_instance()->info_log("signal.action gen report");
		service::get_instance()->gen_report();
        }
}

void	service::start_service()
{
	const int max = sizeof(m_proxy_array) / sizeof(m_proxy_array[0]);
	for(int proxy_id = 0; proxy_id < max - 1; ++proxy_id)
	{
		routine_proxy *rtp = m_proxy_array[proxy_id];

		if(NULL == rtp) continue;
		int ret = pthread_create(&rtp->m_threadid, NULL, routine_proxy::proxy_loop, (void *)proxy_id);

		if(ret != 0)
		{       
			REPORT_EVENT("start_service.pthread_create for proxy=%d result=%d", proxy_id, ret);
			delete	rtp;
			m_proxy_array[proxy_id] = NULL;
			return  ;
		}

		m_routine_proxy_map[rtp->m_threadid] = proxy_id;
		REPORT_EVENT("start_service.pthread_create for proxy=%d,thread_id=%lu",
				proxy_id, rtp->m_threadid);
	}

	struct sigaction        sa_interupted;

	sa_interupted.sa_flags = SA_SIGINFO;
	sigemptyset(&sa_interupted.sa_mask);
	sa_interupted.sa_sigaction = signal_handle;

	sigaction(SIGHUP, &sa_interupted, NULL);
	sigaction(SIGINT, &sa_interupted, NULL);
	sigaction(SIGQUIT, &sa_interupted, NULL);
	sigaction(SIGPIPE, &sa_interupted, NULL);

	routine_proxy *master = m_proxy_array[m_ipc_proxy_id];
	m_routine_proxy_map[pthread_self()] = m_ipc_proxy_id;

	cout << "pthread...=" << pthread_self() << endl;
	m_service_state = service_state_work;
	while(m_service_state == service_state_work)
	{
		master->react();
	}
	cout << "exit" << endl;
	service::get_instance()->info_log("signal.action stop_service");
	service::get_instance()->stop_service();
}

int     service::stop_service()
{
	m_service_state = service_state_stop;

	pthread_t thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		int proxy_id = pos->second;
		for(int n = sizeof(m_ipc_routine) / sizeof(m_ipc_routine[0]) - 1; n > 0; --n)
		{
			if(m_ipc_routine[n] != NULL)
			{
				cout << ".................send stop to :" << n << endl;
				m_ipc_routine[n]->send_stop();
			}
		}

		cout << "ipc_proxy_id=" << proxy_id << endl;
		cout << "m_ipc_proxy_id=" << m_ipc_proxy_id << endl;

		while(m_proxy_exit_count < m_routine_proxy_count)
		{
			m_proxy_array[m_ipc_proxy_id]->react();
		}

		cout << "all stopped" << endl;

		for(int n = sizeof(m_ipc_routine) / sizeof(m_ipc_routine[0]) - 2; n > 0; --n)
		{
			routine_proxy *rtp = m_proxy_array[n];
			if(NULL != rtp)
			{
				void	*exit_ret = NULL;
				pthread_join(rtp->m_threadid, &exit_ret);
			}
		}
	}
	return	0;
}

uint64_t	service::create_udp_channel(uint64_t channel_id, const char *ipaddr, uint16_t port)
{
	routine_proxy *rtp = m_proxy_array[m_ipc_proxy_id];
	active_udp_session_routine* asrt = active_udp_session_routine::create(rtp, 'U', channel_id, ipaddr, port);

	rtp->add_routine(asrt);
	uint64_t routine_id = m_channel_routines[channel_id] = asrt->get_routine_id();
	rtp->get_log_api()->log(log_trace, "create_active_udp_session_routine,channel_id=%lu,routine_id=%lu,"
			"ipaddr=%s,port=%u",
			channel_id, routine_id, ipaddr, port);

	return	routine_id;
}

int     service::create_channel(unsigned channel_id, const char *ipaddr, uint16_t port)
{
	routine_proxy *rtp = m_proxy_array[m_ipc_proxy_id];
	m_channel_routines[channel_id] = 0;
	active_session_routine* asrt = active_session_routine::create(rtp, channel_id, ipaddr, port);

	if(asrt == NULL)
	{
		return  -1;
	}

	rtp->add_routine(asrt);
	rtp->get_log_api()->log(log_trace, "create_active_session_routine,channel_id=%u,"
			"ipaddr=%s,port=%u",
			channel_id, ipaddr, port);
	return	0;
}

log_api*	service::get_logger()
{
	pthread_t       thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		const int8_t    proxy_id = pos->second;
		return	m_proxy_array[proxy_id]->m_log_api;
	}
	else
	{
		REPORT_EVENT("get_logger from unknown thread_id=%lu refused", thread_id);
		return	NULL;
	}
}

void	service::close_routine(uint64_t routine_id)
{
	pthread_t	thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		const int8_t	proxy_id = pos->second;
		m_proxy_array[proxy_id]->close_routine(routine_id);
	}
	else
	{
		REPORT_EVENT("close_routine routine_id=%lu from unknown thread_id=%lu refused",
				routine_id, thread_id);
	}
}

void	service::set_user_data(uint64_t dest_routine_id, void *data)
{
	pthread_t	thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		const int8_t	proxy_id = pos->second;
		m_proxy_array[proxy_id]->set_routine_data(dest_routine_id, data);
	}
	else
	{
		REPORT_EVENT("set_user_data dest_routine_id=%lu,data=%lu from unknown thread_id=%lu refused",
				dest_routine_id, (unsigned long)data, thread_id);
	}
}

void	service::settle_routine(uint64_t dest_routine_id, uint16_t dest_proxy_id)
{
	pthread_t	thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		const int8_t	proxy_id = pos->second;
		m_proxy_array[proxy_id]->settle_routine(dest_routine_id, dest_proxy_id);
	}
	else
	{
		REPORT_EVENT("settle_routine dest_routine_id=%lu,dest_proxy_id=%u "
				"from unknown thread_id=%lu was denied",
				dest_routine_id, dest_proxy_id, thread_id);
	}
}

void	service::send_message(uint16_t dest_proxy_id, int64_t dest_routine_id, const void *data, unsigned len)
{
	pthread_t	thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		const int8_t	proxy_id = pos->second;
		m_proxy_array[proxy_id]->send_message(dest_proxy_id, dest_routine_id, data, len);
	}
	else
	{
		REPORT_EVENT("send_message dest_proxy_id=%u,dest_routine_id=%lu "
				"from unknown thread_id=%lu refused",
				dest_proxy_id, dest_routine_id, thread_id);
	}
}

void	service::do_register_timer(const timer_info &ti)
{
	pthread_t	thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		const int8_t	proxy_id = pos->second;
		m_proxy_array[proxy_id]->register_timer(ti);
	}
	else
	{
		REPORT_EVENT("do_register_timer unique_timer_id=%lu from unknown thread_id=%lu refused",
				ti.get_unique_timer_id(), thread_id);
	}
}

void	service::do_delete_timer(uint64_t unique_timer_id)
{
	pthread_t	thread_id = pthread_self();
	hss_map_t::iterator pos = m_routine_proxy_map.find(thread_id);
	if(pos != m_routine_proxy_map.end())
	{
		const int8_t	proxy_id = pos->second;
		m_proxy_array[proxy_id]->delete_timer(unique_timer_id);
	}
	else
	{
		REPORT_EVENT("do_delete_timer unique_timer_id=%lu from unknown thread_id=%lu refused",
				unique_timer_id, thread_id);
	}
}

int	service::create_tcp_service_routine(char category, uint64_t routine_flag, const char *ipaddr, uint16_t port)
{
	routine_proxy *rtp = m_proxy_array[m_ipc_proxy_id];
	tcp_service_routine* tsrt = tcp_service_routine::create(rtp, category, routine_flag, ipaddr, port);
	if(tsrt == NULL)
	{
		return	-1;
	}

	rtp->add_routine(tsrt);
	rtp->get_log_api()->log(log_trace, "create_tcp_service_routine,routine_type=%d,"
			"ipaddr=%s,port=%u",
			category, ipaddr, port);
	return	0;
}

int	service::create_udp_service_routine(char category, uint64_t routine_flag, const char *ipaddr, uint16_t port)
{
	routine_proxy *rtp = m_proxy_array[m_ipc_proxy_id];
        udp_service_routine* usrt = udp_service_routine::create(rtp, category, routine_flag, ipaddr, port);
        if(usrt == NULL)
        {
                return  -1;
        }

        rtp->add_routine(usrt);
        rtp->get_log_api()->log(log_trace, "create_udp_service_routine,routine_type=%d,"
                        "ipaddr=%s,port=%u",
                        category, ipaddr, port);
        return  0;
}

int     service::send_via_channel(uint64_t channel_id, uint32_t msg_type, const void *data, unsigned data_len)
{
	service_msg_header	header;
	header.msg_type = msg_type;
	header.msg_size = data_len + sizeof(service_msg_header);
	header.reserved = 1;
	header.sequence = 0;

	string	ss((const char *)&header, sizeof(header));
	ss.append((const char *)data, data_len);

	routine_proxy *rtp = m_proxy_array[m_ipc_proxy_id];
        rtp->get_log_api()->log(log_trace, "send_via_channel,channel_id=%lu,routine_id=%lu,msg_type=%u,data_len=%u",
                        channel_id, m_channel_routines[channel_id], msg_type, data_len);
	send_message(m_ipc_proxy_id, m_channel_routines[channel_id], ss.c_str(), ss.size());
	return	0;
}
}
