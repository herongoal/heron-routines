#include "sample.h"
#include <cstdlib>
#include <pthread.h>


sample_service *sample_service::m_fight_service = NULL;

sample_service::~sample_service()
{
	pthread_rwlock_destroy(&m_rwlock);
}

sample_service::sample_service(const string &log_file, log_level level, int8_t proxy_count):
	service(log_file, 2000, level, proxy_count)
{
	pthread_rwlock_init(&m_rwlock, NULL);
	m_fight_service = this;
}

void	sample_service::delete_timer(int64_t unique_timer_id)
{
	m_timer_info.erase(unique_timer_id);
	do_delete_timer(unique_timer_id);
}

int64_t	sample_service::register_timer(int64_t fight_id, int interval, int times)
{
	static int64_t  s_unique_timer_id = 1;
	int64_t unique_timer_id = __sync_fetch_and_add(&s_unique_timer_id, 1);

	using namespace hrts;
	timer_info ti(unique_timer_id, interval, times);
	service::do_register_timer(ti);

	m_timer_info[unique_timer_id] = fight_id;
	return	unique_timer_id;
}

void    sample_service::init()
{
	create_tcp_service_routine('s', 8601, m_listen_ipaddr.c_str(), m_listen_port);
	create_udp_service_routine('s', 8601, m_listen_ipaddr.c_str(), m_listen_port);
	register_timer(0, 1000, -1);

	start_service();
}

void	sample_service::on_routine_read_hup(routine_proxy* proxy, routine* r)
{
}

void    sample_service::on_routine_peer_hup(routine_proxy* proxy, routine* r)
{
}

void    sample_service::on_routine_created(uint64_t routine_type, uint64_t routine_id)
{
}

void    sample_service::on_routine_error(routine_proxy* proxy, routine* r)
{
}

void    sample_service::on_routine_closed(routine *rt)
{
}

void    sample_service::on_timer(routine_proxy *proxy, const timer_info &ti)
{
	int64_t td = ti.get_unique_timer_id();
	map<int64_t, int64_t>::iterator pos = m_timer_info.find(td);

	if(pos == m_timer_info.end())
	{
		proxy->get_log_api()->log(log_error, "delete_timer unique_timer_id=%ld", td);	
		delete_timer(td);
	}
	else
	{
		cout << "on_timer" << endl;
	}
}

int	main(int argc, char *argv[])
{
	sample_service ss("sample_service.log", log_trace, 4);
	ss.m_listen_ipaddr = "0.0.0.0";
	ss.m_listen_port = 40009;
	ss.init();
	return	0;
}
