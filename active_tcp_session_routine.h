#ifndef _HRTS_ACTIVE_SESSION_ROUTINE_H_
#define _HRTS_ACTIVE_SESSION_ROUTINE_H_


#include "routine.h"
#include "circular_buffer.h"
#include "log_api.h"


#include <stdarg.h>
#include <stdint.h>
#include <list>


namespace	hrts{
using	std::list;


class	routine_proxy;


class	active_session_routine:public routine{
public:
	static	active_session_routine*	create(routine_proxy *proxy,
			uint64_t user_flag,
			const char *ipaddr, uint16_t port);
	
	virtual	~active_session_routine();

	int     inspect();

	/**
	 * In parent class routine get_events is declared as pure virtual,
	 * implement it to return epoll events for epoll registering.
	 */
	virtual	uint32_t	get_events();

	virtual bool            vital(){return	false;}

	void	check_conn_state(int err);
	/**
	 * In parent class routine on_readable is declared as pure virtual,
	 * implement it to process epollin event.
	 */
	virtual int	on_readable();

	/**
	 * In parent class routine on_writable is declared as pure virtual,
	 * implement it to process epollout event.
	 */
	virtual int	on_writable();

	/**
	 * In parent class routine on_error is declared as pure virtual,
	 * implement it to process epollerr event, which is possibly triggered
	 * by networking failure and so on.
	 */
	virtual void	on_error();

	/**
	 * In parent class routine on_read_hup is declared as pure virtual,
	 * implement it to process epollrdhup event, which is offten triggered by
	 * remote peer shut down writing half of connection and so on.
	 */
	virtual void	on_read_hup();

	/**
	 * In parent class routine on_peer_hup is declared as pure virtual,
	 * implement it to process epollhup event,which is offten triggered 
	 * by crushing of remote peer and so on.
	 */
	virtual void	on_peer_hup();

private:
	bool			m_established;
	active_session_routine(routine_proxy *proxy, uint64_t user_flag);
	friend	class	routine_proxy;
};//end of class active_session_routine
}//end of namespace hrts


#endif //_HRTS_ACTIVE_SESSION_ROUTINE_H_
