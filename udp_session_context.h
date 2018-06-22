#ifndef _HRTS_UDP_SESSION_CONTEXT_H_
#define _HRTS_UDP_SESSION_CONTEXT_H_


#include "routine.h"
#include "udp_define.h"


#include <linux/types.h>
#include <netinet/in.h>


#include <stdarg.h>
#include <stdint.h>
#include <vector>


namespace	hrts{
class	routine_proxy;
using	namespace std;


class	udp_session_context:public routine{
public:
	static	udp_session_context*	create(routine_proxy *proxy, int routine_type, int fd);

	virtual ~udp_session_context();

	int	dispose(const char *data, unsigned len);
	int	forward(uint64_t timestamp);

	virtual	int	inspect();

	virtual	bool    append_send_data(const void *data, unsigned len);

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
	static const int	m_timeout_ms = 500;

	int			dispose_push(const fragment_header *sfh, const void *data, unsigned len);
	int			do_nonblock_send(const void *data, unsigned len);

	bool    	process_peer_nrs(unsigned cmd, unsigned seq, uint32_t nrs);
	bool		dispose_ack(const fragment_header *sf);

	void    	makeup_fragment();

	friend	class	udp_service_routine;

	uint64_t		m_conversation_id;


	uint32_t		m_peer_next_recv_seq;
	uint32_t		m_next_send_seq;
	list<fragment_info>	m_send_fragment;
	uint32_t		m_peer_recv_window;
	uint32_t		m_send_naf;

	uint32_t		m_recv_wait_seq;
	list<fragment_info>	m_recv_fragment;

	int64_t			m_inspect_peer_time;
	int64_t			m_peer_respond_time;
	int			m_inspect_times;
	int			m_inspect_timeout_times;

	int64_t			m_last_resp_time;
	int64_t			m_last_sync_time;
	map<uint32_t, uint32_t>	m_sending_ack;

	list<uint32_t>		m_alive_resp;
	list<uint32_t>		m_session_resp;

	bool			m_forward_required;
	const	uint32_t	m_conv_window;


	int32_t			m_awful_behaviors;
	int32_t			m_ack_times;
	int32_t			m_bad_ack_times;
	int32_t			m_extra_ack_times;
	uint32_t		m_session_conv;

	uint64_t		m_total_transfer_rtt;
	uint32_t		m_max_transfer_rtt;
	uint64_t		m_transfer_times;
	list<int>		m_push_srtt;
	
	udp_session_context(routine_proxy *proxy, char category, int fd);
	static	void	set_flags(routine_proxy *proxy, int fd);
};//end of class udp_session_context
}//end of namespace hrts 


#endif //_HRTS_UDP_SESSION_CONTEXT_H_
