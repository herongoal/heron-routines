#ifndef _HRTS_TCP_PASSIVE_SESSION_ROUTINE_H_
#define _HRTS_TCP_PASSIVE_SESSION_ROUTINE_H_


#include "routine.h"


#include <linux/types.h>


#include <stdarg.h>
#include <stdint.h>


namespace	hrts{
class	routine_proxy;


class	tcp_passive_session_routine:public routine{
public:
	static	tcp_passive_session_routine*	create(routine_proxy *proxy, char category, int fd);

	virtual ~tcp_passive_session_routine();

	int     inspect();

	/**
	 * In parent class routine get_events is declared as pure virtual,
	 * implement it to return epoll events for epoll registering.
	 */
	virtual	uint32_t	get_events();

        virtual bool            vital(){return  false;}

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
	tcp_passive_session_routine(routine_proxy *proxy, char category, int fd);
	static	void	set_flags(routine_proxy *proxy, int fd);
};//end of class tcp_passive_session_routine
}//end of namespace hrts


#endif //_HRTS_TCP_PASSIVE_SESSION_ROUTINE_H_
