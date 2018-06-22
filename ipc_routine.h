#ifndef _HRTS_IPC_ROUTINE_H_
#define _HRTS_IPC_ROUTINE_H_


#include "routine.h"


#include <linux/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <list>
#include <string>


namespace	hrts{
class	routine_proxy;


class	ipc_routine:public routine{
public:
	ipc_routine(routine_proxy *proxy, uint16_t routine_type, int fd);

	virtual	~ipc_routine();

	virtual		int		inspect()
	{
		return	0;
	}

	bool	append_send_data(const void *data, const unsigned data_len);
	bool	send_stop();
	bool	notify_stopped();

	bool	transfer_data(uint8_t dst_proxy, const void *data, const unsigned data_len);

	bool	send_msg(uint8_t dst_proxy, uint64_t dst_routine,
			const void *data,
			const unsigned data_len);

	bool	settle_routine(uint8_t dest_proxy, routine* rt);

	/**
	 * In parent class routine get_events is declared as pure virtual,
	 * implement it to return epoll events for epoll registering.
	 */
	virtual	uint32_t	get_events();

        virtual bool            vital(){return  true;}

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
	 * implement it to process epollrdhup event, for ipc_routine
	 * this will never happen.
	 */
	virtual void	on_read_hup();

	/**
	 * In parent class routine on_peer_hup is declared as pure virtual,
	 * implement it to process epollhup event, for ipc_routine
	 * this will never happen.
	 */
	virtual void	on_peer_hup();

private:
	std::list<std::string>	m_send_list;
};
}


#endif //_HRTS_IPC_ROUTINE_H_
