#ifndef	_HRTS_SERVICE_H_
#define	_HRTS_SERVICE_H_


#include <tr1/unordered_map>
#include <iostream>
#include <string>
#include <signal.h>
#include <cstdlib>


#include <unistd.h>


#include "circular_buffer.h"
#include "routine.h"
#include "routine_proxy.h"
#include "timer.h"


namespace hrts{
using	std::tr1::unordered_map;
#pragma	pack(1)
struct	service_msg_header{
	service_msg_header():msg_size(0), msg_type(0), reserved(0), sequence(0)
	{
	}

	service_msg_header& operator=(const service_msg_header &smh)
	{
		this->msg_size = smh.msg_size;
		this->msg_type = smh.msg_type;
		this->reserved = smh.reserved;
		this->sequence = smh.sequence;
		return	*this;
	}

	service_msg_header(const service_msg_header &smh)
	{
		this->msg_size = smh.msg_size;
		this->msg_type = smh.msg_type;
		this->reserved = smh.reserved;
		this->sequence = smh.sequence;
	}
	uint32_t	msg_size;
	uint32_t	msg_type;
	uint32_t	reserved;
	uint32_t	sequence;
};
#pragma	pack()


class	service{
public:
	virtual	int	process(routine_proxy *proxy, routine *r, circular_buffer *buf);

	/*
	virtual	int	get_dest_proxy(log_api *api, routine *r,
			const service_msg_header &header,
			const char *msg, unsigned msg_len) = 0;
	*/

	virtual	int	process(log_api *api, routine *r,
			const service_msg_header &header,
			const char *msg, unsigned msg_len) = 0;

	virtual	int64_t	register_timer(int64_t unique_timer_id, int interval, int times) = 0;
	virtual	void	delete_timer(int64_t unique_timer_id) = 0;

	
	void		send_message(uint16_t proxy_id, int64_t routine_id, const void *data, unsigned len);
	void		close_routine(uint64_t routine_id);

	log_api*        get_logger();
	log_level	get_log_level(){return	m_log_level;}

	service(const std::string &log_file, size_t slice_kb, log_level level, unsigned proxy_count);
	virtual	~service();
	static	service	*get_instance() 
	{
		return m_service_instance;
	}

	void		trace_log(const char *format, ...);
	void		debug_log(const char *format, ...);
	void		info_log(const char *format, ...);
	void		error_log(const char *format, ...);
	void		warn_log(const char *format, ...);
	void		fatal_log(const char *format, ...);

	void		create_routine_proc(uint64_t user_flag, uint64_t routine_id);
	void		close_routine_proc(uint64_t user_flag, uint64_t routine_id);

	static	void	signal_handle(int sigid, siginfo_t *si, void *unused);
	int		stop_service();

	virtual	void	on_timer(routine_proxy *proxy, const timer_info &ti) = 0;
	virtual	void	on_routine_read_hup(routine_proxy* proxy, routine* r) = 0;
	virtual	void	on_routine_peer_hup(routine_proxy* proxy, routine* r) = 0;
	virtual	void	on_routine_error(routine_proxy* proxy, routine* r) = 0;
	virtual	void	on_routine_created(uint64_t user_flag, uint64_t routine_id) = 0;
	virtual	void	on_routine_closed(routine* rt) = 0;

	virtual	int	create_tcp_service_routine(char category, uint64_t routine_flag, const char *ipaddr, uint16_t port);
	virtual	int	create_udp_service_routine(char category, uint64_t routine_flag, const char *ipaddr, uint16_t port);

	virtual	int	create_channel(unsigned channel_id, const char *ipaddr, uint16_t port);
	virtual	uint64_t	create_udp_channel(uint64_t channel_id, const char *ipaddr, uint16_t port);
	virtual	int	send_via_channel(uint64_t channel_id, uint32_t msg_type, const void *data, unsigned data_len);

	virtual	void	start_service();

	void		reload_config()
	{
	}

	void		gen_report()
	{
	}

	void		settle_routine(uint64_t routine_id, uint16_t proxy_id);
	void		set_user_data(uint64_t routine_id, void *data);

	void		proc_proxy_stop(uint16_t proxy_id)
	{
		cout << "proc_proxy_stop=" << proxy_id << endl;
		m_proxy_exit_count += 1;
	}

protected:
	void		do_register_timer(const timer_info &info);
	void		do_delete_timer(uint64_t unique_timer_id);

private:
	typedef	unordered_map<uint64_t, uint8_t>	hss_map_t;
	hss_map_t		m_routine_proxy_map;
	log_level		m_log_level;
	int			m_proxy_exit_count;
	int			m_routine_proxy_count;

	int			m_log_proxy_id;
	int			m_ipc_proxy_id;

	routine_proxy*		m_proxy_array[32];
	ipc_routine*		m_ipc_routine[32];
	log_routine*		m_log_routine[32];

private:
	map<uint64_t, uint64_t>	m_channel_routines;
	enum	service_state{
		service_state_init = 0,
		service_state_work = 1,
		service_state_stop = 2
	}m_service_state;

private:
	static	service	*m_service_instance;
	friend	class	passive_session_routine;
	friend	class	active_session_routine;
	friend	class	routine_proxy;
};
}


#endif //_HRTS_SERVICE_H_
