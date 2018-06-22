#include "service_define.h"
#include "routine_proxy.h"
#include "service.h"
#include "routine.h"


#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>


#include <cstdio>
#include <cstring>
#include <iostream>
using namespace std;


namespace hrts{
routine_proxy::routine_proxy(unsigned proxy_id, bool auto_resume, log_level level, file_writer *log_writer):
	m_log_routine(NULL),
	m_ipc_routine(NULL),
        m_log_writer(log_writer),
	m_sync_timers(1, 100, gen_monotonic_ms()),
	m_check_timers(500, 200, gen_monotonic_ms()),
        m_epoll_fd(-1), m_proxy_id(proxy_id), m_log_api(NULL),
        m_auto_resume(auto_resume),
        m_threadid(0)
{
        m_log_api = new log_api(this, level);

        if((m_epoll_fd = epoll_create(1024)) < 0)
        {
                REPORT_EVENT("routine_proxy.epoll_create for proxy=%d failed,"
                                "errno=%d,errmsg=%s",
                                m_proxy_id, errno, strerror(errno));
		throw	std::bad_alloc();
        }

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, m_ipc_sockpair_fd) < 0
			|| socketpair(AF_UNIX, SOCK_DGRAM, 0, m_log_sockpair_fd) < 0
	   )
	{
		REPORT_EVENT("routine_proxy.socketpair for proxy=%d failed,"
				"errno=%d,errmsg=%s",
				m_proxy_id, errno, strerror(errno));
		throw	std::bad_alloc();
	}

        const   int     ipc_buf_len = 1024 * 1024;
        setsockopt(m_ipc_sockpair_fd[0], SOL_SOCKET, SO_SNDBUF, &ipc_buf_len, sizeof(ipc_buf_len));
        setsockopt(m_ipc_sockpair_fd[0], SOL_SOCKET, SO_RCVBUF, &ipc_buf_len, sizeof(ipc_buf_len));
        setsockopt(m_ipc_sockpair_fd[1], SOL_SOCKET, SO_SNDBUF, &ipc_buf_len, sizeof(ipc_buf_len));
        setsockopt(m_ipc_sockpair_fd[1], SOL_SOCKET, SO_RCVBUF, &ipc_buf_len, sizeof(ipc_buf_len));

        setsockopt(m_log_sockpair_fd[0], SOL_SOCKET, SO_SNDBUF, &ipc_buf_len, sizeof(ipc_buf_len));
        setsockopt(m_log_sockpair_fd[0], SOL_SOCKET, SO_RCVBUF, &ipc_buf_len, sizeof(ipc_buf_len));
        setsockopt(m_log_sockpair_fd[1], SOL_SOCKET, SO_SNDBUF, &ipc_buf_len, sizeof(ipc_buf_len));
        setsockopt(m_log_sockpair_fd[1], SOL_SOCKET, SO_RCVBUF, &ipc_buf_len, sizeof(ipc_buf_len));

	if(-1 == fcntl(m_ipc_sockpair_fd[0], F_SETFL, O_NONBLOCK)
			|| -1 == fcntl(m_ipc_sockpair_fd[1], F_SETFL, O_NONBLOCK)
			|| -1 == fcntl(m_log_sockpair_fd[0], F_SETFL, O_NONBLOCK)
			|| -1 == fcntl(m_log_sockpair_fd[1], F_SETFL, O_NONBLOCK))
	{
		REPORT_EVENT("routine_proxy.fcntl for proxy=%d failed,errno=%d,errmsg=%s",
				m_proxy_id, errno, strerror(errno));
		throw	std::bad_alloc();
	}

	m_log_routine = log_routine::create(this, 'l', m_log_sockpair_fd[0]);
	if(NULL == m_log_routine)
	{
		REPORT_EVENT("routine_proxy.create log_routine for proxy=%d bad_alloc", m_proxy_id);
		throw	std::bad_alloc();
	}

	add_routine(m_log_routine);

	m_ipc_routine = new (std::nothrow)ipc_routine(this, 'c', m_ipc_sockpair_fd[0]);
	if(NULL == m_ipc_routine)
	{
		REPORT_EVENT("routine_proxy.create ipc_routine for proxy=%d bad_alloc", m_proxy_id);
		throw	std::bad_alloc();
	}

	add_routine(m_ipc_routine);
}

bool        routine_proxy::transfer_msg(int8_t dest_proxy_id, const void *data, unsigned len)
{
	ipc_routine *irt = service::get_instance()->m_ipc_routine[dest_proxy_id];

	if(NULL != irt)
	{
		//return        irt->transfer_msg(dest_proxy_id, data, len);
	}

	return        false;
}
bool        routine_proxy::send_message(int8_t dest_proxy_id, uint64_t dest_routine_id, const void *data, unsigned len)
{
	if(m_proxy_id == dest_proxy_id)
	{
		routine *rt = (routine *)m_routine_mgr.search_elem(dest_routine_id);

		if(NULL == rt)
		{
			m_log_api->log(log_error, "send_message to routine=%ld,dest_proxy_id=%u not exist",
					dest_routine_id, dest_proxy_id);
			return        false;
		}

		if(!rt->append_send_data(data, len))
		{
			m_log_api->log(log_error, "send_message to routine=%ld,dest_proxy_id=%u failed",
					dest_routine_id, dest_proxy_id);
			return        false;
		}

		return        true;
	}

	ipc_routine *irt = service::get_instance()->m_ipc_routine[dest_proxy_id];

	if(NULL != irt)
	{
		return        irt->send_msg(dest_proxy_id, dest_routine_id, data, len);
	}

	return        false;
}

bool        routine_proxy::register_timer(const timer_info &ti)
{
        if(ti.m_interval < 100)
        {
                return  m_sync_timers.register_timer(m_log_api, gen_monotonic_ms(), ti);
        }
        else
        {
                return  m_check_timers.register_timer(m_log_api, gen_monotonic_ms(), ti);
        }
}

void    routine_proxy::set_routine_data(uint64_t routine_id, void *data)
{
        routine *rt = (routine *)m_routine_mgr.search_elem(routine_id);
        if(NULL != rt)
        {
                rt->set_user_data(data);
        }
}

void        routine_proxy::close_routine(uint64_t routine_id)
{
        routine *rt = (routine *)m_routine_mgr.erase_elem(routine_id);

        if(NULL != rt)
        {
                delete  rt;
        }
        else
        {
                m_log_api->log(log_error, "close_routine routine_id=%ld not found",
                                routine_id);
        }
}

void	routine_proxy::delete_timer(uint64_t unique_timer_id)
{
        m_sync_timers.delete_timer(m_log_api, unique_timer_id);
        m_check_timers.delete_timer(m_log_api, unique_timer_id);
}

void    routine_proxy::modify_events(routine *rt)
{
        struct  epoll_event ev;

	if((ev.events = rt->get_events()) != 0 && rt->m_fd >= 0)
	{
		int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, rt->m_fd, &ev);
		if(0 != ret)
		{
			m_log_api->log(rt, log_error, "modify_events,errno=%d", errno);
		}
	}
}

void    routine_proxy::unregister_events(routine *rt)
{
        struct  epoll_event ev;
        ev.events = EPOLLIN;

        int ret = epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, rt->m_fd, &ev);
        if(0 == ret)
        {
                m_log_api->log(rt, log_trace, "unregister_events done");
        }
        else
        {
                m_log_api->log(rt, log_error, "unregister_events,errno=%d,strerror(errno)=%s",
                                errno, strerror(errno));
        }
}

void        routine_proxy::send_log(const char *data, unsigned len)
{
        if(NULL != m_log_writer)
        {
                m_log_writer->append(data, len);
        }
        else
        {
                m_log_routine->append_send_data(data, len);
        }
}
void        routine_proxy::settle_routine(uint64_t routine_id, uint16_t dest_proxy_id)
{
        if(dest_proxy_id == m_proxy_id)
        {
                m_log_api->log(log_trace, "settle_routine routine_id=%lu to src_proxy=%u",
                                routine_id, dest_proxy_id);
                return        ;
        }

        routine *rt = (routine *)m_routine_mgr.search_elem(routine_id);

        if(NULL != rt)
        {
		m_routine_mgr.erase_elem(routine_id);
                unregister_events(rt);
                m_log_api->log(rt, log_trace, "settle_routine.unregister_events=%d",
                                rt->get_events());

                static const int master_proxy_id = 1;
                if(m_proxy_id == master_proxy_id)
                {
			ipc_routine *irt = service::get_instance()->m_ipc_routine[dest_proxy_id];
                        irt->settle_routine(dest_proxy_id, rt);
                }
                else
                {
                        m_ipc_routine->settle_routine(dest_proxy_id, rt);
                }
        }

        
}
void    routine_proxy::add_routine(routine *rt)
{
	m_routine_mgr.insert_elem(rt->m_routine_id, rt);
        rt->m_proxy = this;

        if(rt->get_events() != 0)
        {
		const int events = rt->get_events();
                struct  epoll_event ev;
		ev.events = events;
		ev.data.u64 = rt->m_routine_id;

                if(epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, rt->m_fd, &ev) == 0)
                {
                        m_log_api->log(rt, log_trace, "add_routine.epoll_ctl,events=%d",
                                        events);
                }
                else
                {
                        m_log_api->log(rt, log_error, "add_routine.epoll_ctl,events=%d, errno=%d",
                                        events, errno);
                }
        }
	else
	{
		m_log_api->log(rt, log_trace, "add_routine no_events");
	}

        if(rt->get_proxy_id() == m_proxy_id && rt->m_recv->data_len() > 0)
        {
                rt->on_readable();
        }
}

void	routine_proxy::react()
{
        struct  epoll_event	arr_events[512];
	size_t	fetch_num = sizeof(arr_events) / sizeof(arr_events[0]);

	if(m_routine_mgr.elems() < fetch_num) fetch_num = m_routine_mgr.elems();
	if(fetch_num > 0)
	{
		int result = epoll_wait(m_epoll_fd, arr_events, fetch_num, 0);
		if(result < 0 && errno != EINTR)
		{
			m_log_api->log(log_error, "process_events.epoll_wait,epoll_fd=%d,fetch_num=%u,%d occurred,errmsg=%s",
					m_epoll_fd, fetch_num,errno, strerror(errno));
		}

		for(int n = 0; n < result; ++n)
		{
			struct  epoll_event *pe = arr_events + n;
			process_events(pe->data.u64, pe->events);
		}
	}

	struct	timespec ts, remain;
	ts.tv_sec = 0;
	ts.tv_nsec = 2 * 1000 * 1000;
	nanosleep(&ts, &remain);

	inspect();
	process_timers();
}

void* routine_proxy::proxy_loop(void* arg)
{
	const int proxy_id = (int)(uint64_t)arg;
	routine_proxy *rp = service::get_instance()->m_proxy_array[proxy_id];

	if(rp->m_auto_resume)
	{
		sigset_t        sig_set;
		sigemptyset(&sig_set);
		sigaddset(&sig_set, SIGTERM);
		sigaddset(&sig_set, SIGQUIT);
		sigaddset(&sig_set, SIGINT);
		sigaddset(&sig_set, SIGHUP);
		sigaddset(&sig_set, SIGPIPE);
		sigaddset(&sig_set, SIGUSR1);
		sigaddset(&sig_set, SIGUSR2);
		sigaddset(&sig_set, SIGXFSZ);
		sigaddset(&sig_set, SIGTRAP);
		pthread_sigmask(SIG_BLOCK, &sig_set, NULL);
	}

	do{
		rp->react();
	}while(rp->should_run());

	return        rp->stop_proxy_loop();
}
void        routine_proxy::inspect()
{
	uint64_t monotonic_ms = gen_monotonic_ms();

        for(size_t n = 0; n < m_routine_mgr.elems(); ++n)
        {
                routine *rt = (routine *)m_routine_mgr.access_elem();

                if(rt->m_last_inspect_time + 10 < monotonic_ms)
                {
                        //service::get_instance()->on_routine_closed(rt);
                        //delete        rt;
                        int r = rt->inspect();
			rt->m_last_inspect_time = gen_monotonic_ms();
                }
		else
		{
			break;
		}
                m_routine_mgr.forward_elem();
        }
}

void        routine_proxy::process_timers()
{
	int64_t	monotonic_ms = gen_monotonic_ms();
        timer_info *ti = NULL;
        while((ti = m_sync_timers.get_latest_timer(monotonic_ms)) != NULL)
        {
                if(ti->next_exec_time() > monotonic_ms)
                {
                        break;
                }
                else
                {
			service::get_instance()->on_timer(this, *ti);
                        int64_t	shift = abs(ti->next_exec_time() - monotonic_ms);

                        if(shift > ti->m_max_shift)
                        {
                                ti->m_max_shift = shift;
                        }

                        ti->m_total_shift += shift;
                        ti->m_execs += 1;
                        m_sync_timers.forward_timer(m_log_api, monotonic_ms);
                }
        }

        while((ti = m_check_timers.get_latest_timer(monotonic_ms)) != NULL)
        {
                if(ti->next_exec_time() > monotonic_ms)
                {
                        break;
                }
                else
                {
			service::get_instance()->on_timer(this, *ti);
                        int64_t	shift = abs(ti->next_exec_time() - monotonic_ms);

                        if(shift > ti->m_max_shift)
                        {
                                ti->m_max_shift = shift;
                        }

                        ti->m_total_shift += shift;
                        ti->m_execs += 1;
                        m_check_timers.forward_timer(m_log_api, monotonic_ms);
                }
        }
}

void        routine_proxy::process_events(const uint64_t routine_id, const unsigned events)
{
        if((events & EPOLLIN) != 0)
        {
                routine *rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL == rt)
                {
                        //this will never happen unless bug does exist in routine_proxy
                        m_log_api->log(log_error, "readable routine not found,"
                                        "routine_id=%ld,events=%u",
                                        routine_id, events);
                        return        ;
                }

                if(rt->on_readable() < 0)
                {
                        //maybe the routine has already been deleted
			rt = (routine *)m_routine_mgr.search_elem(routine_id);
                        if(NULL != rt)
                        {
				cout << "readable error" << endl;
                                unregister_events(rt);
                                close_routine(routine_id);
                        }
                        return        ;
                }
        }

        if(events & EPOLLERR)
        {
		routine *rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL == rt)
                {
                        m_log_api->log(log_error, "error routine not found,"
                                        "routine_id=%ld,events=%u",
                                        routine_id, events);
                        return        ;
                }

                rt->on_error();
                //maybe the routine has already been deleted
		rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL == rt)
                {
                        return        ;
                }
                service::get_instance()->on_routine_error(this, rt);

                //maybe the routine has already been deleted
		rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL != rt)
                {
                        unregister_events(rt);
                        close_routine(routine_id);
                }
                return        ;
        }

        if(events & EPOLLRDHUP)
        {
		routine *rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL == rt)
                {
                        m_log_api->log(log_error, "readhup routine not found,"
                                        "routine_id=%ld,events=%u",
                                        routine_id, events);
                        return        ;
                }

                rt->on_read_hup();
                //maybe the routine has already been deleted
		rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL == rt)
                {
                        return        ;
                }
                service::get_instance()->on_routine_read_hup(this, rt);

                //maybe the routine has already been deleted
		rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL != rt)
                {
                        unregister_events(rt);
                        close_routine(routine_id);
                }

                return        ;
        }

        if(events & EPOLLHUP)
        {
		routine *rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL == rt)
                {
                        m_log_api->log(log_error, "peerhup routine not found,"
                                        "routine_id=%ld,events=%u",
                                        routine_id, events);
                        return        ;
                }

                rt->on_peer_hup();
                //maybe the routine has already been deleted
		rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL == rt)
                {
                        return        ;
                }
                service::get_instance()->on_routine_peer_hup(this, rt);

                //maybe the routine has already been deleted
		rt = (routine *)m_routine_mgr.search_elem(routine_id);
                if(NULL != rt)
                {
                        unregister_events(rt);
                        close_routine(routine_id);
                }

                return        ;
        }

        //writable process
        if((events & EPOLLOUT) == 0)
        {
                return        ;
        }

	routine *rt = (routine *)m_routine_mgr.search_elem(routine_id);
        if(NULL == rt)
        {
                m_log_api->log(log_error, "writable routine not found,"
                                "routine_id=%ld,events=%u",
                                routine_id, events);
                return        ;
        }

        rt->m_writable = true;
        int ret = rt->on_writable();
        if(ret < 0)
        {
                unregister_events(rt);
                close_routine(routine_id);
                return        ;
        }

        if(rt->m_writable)
        {
                //delete writable event
                struct  epoll_event ev;
                ev.events = rt->get_events() & (~EPOLLOUT);
                ev.data.u64 = rt->m_routine_id;
                if(epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, rt->m_fd, &ev) != 0)
                {
                        m_log_api->log(rt, log_trace, "failed to depress writable=%u",
                                        ev.events);
                }
        }
}

uint64_t        routine_proxy::gen_monotonic_ms()
{
        clock_gettime(CLOCK_MONOTONIC, &m_timespec);
        return        m_timespec.tv_sec * 1000 + m_timespec.tv_nsec / 1000000;
}

uint64_t        routine_proxy::current_utc_ms()
{
        clock_gettime(CLOCK_MONOTONIC, &m_timespec);
        return        m_timespec.tv_sec * 1000 + m_timespec.tv_nsec / 1000000;
}

routine_proxy::~routine_proxy()
{
        m_log_api->log(log_trace, "~routine_proxy");

        
        while(m_routine_mgr.forward_elem())
        {
                routine *rt = (routine *)m_routine_mgr.access_elem();

		if(NULL == rt) break;

		m_routine_mgr.erase_elem(rt->m_routine_id);
		if(!rt->vital())
		{
                	delete rt;
		}
        }

	if(m_ipc_routine != NULL)
	{
		delete m_ipc_routine;
		m_ipc_routine = NULL;
	}

	if(m_log_routine != NULL)
	{
		delete m_log_routine;
		m_log_routine = NULL;
	}

	delete  m_log_api;
	m_log_api = NULL;

	if(NULL != m_log_writer)
        {
                delete m_log_writer;
                m_log_writer = NULL;
        }
}

void*        routine_proxy::stop_proxy_loop()
{
	m_log_api->log(log_info, "stop_proxy_loop,proxy_id=%u", m_proxy_id);
	m_ipc_routine->notify_stopped();
        return	NULL;
}
}
