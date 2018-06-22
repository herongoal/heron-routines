#ifndef _HRTS_UDP_SERVICE_ROUTINE_H_
#define _HRTS_UDP_SERVICE_ROUTINE_H_


#include "routine.h"
#include "udp_define.h"
#include "udp_session_context.h"


#include <linux/types.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>


namespace	hrts{
class	routine_proxy;


class	udp_service_routine:public routine{
public:
	static	udp_service_routine*	create(routine_proxy *proxy, char category, uint64_t user_flag,
                const char *ipaddr, uint16_t port);

	virtual	~udp_service_routine();

	virtual int     inspect();

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
	 * implement it to process epollerr event.
	 */
	virtual void	on_error();

	/**
	 * In parent class routine on_read_hup is declared as pure virtual,
	 * implement it to process epollrdhup event, for service_routine
	 * this will never happen.
	 */
	virtual void	on_read_hup();

	/**
	 * In parent class routine on_peer_hup is declared as pure virtual,
	 * implement it to process epollhup event, for service_routine
	 * this will never happen.
	 */
	virtual void	on_peer_hup();

private:
	static	const	int	m_socket_buffer_size;
	udp_service_routine(routine_proxy *proxy, char category, uint64_t user_flag, int fd);
};
}


#endif //_HRTS_UDP_SERVICE_ROUTINE_H_
