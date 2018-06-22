#ifndef _HRTS_ROUTINE_H_
#define _HRTS_ROUTINE_H_


#include "circular_buffer.h"


#include <linux/types.h>
#include <netinet/in.h>


#include <stdarg.h>
#include <stdint.h>
#include <list>


namespace        hrts{
using        std::list;


class        routine_proxy;
class        timer_info;


class        routine{
public:
        routine(routine_proxy *proxy, char category, uint64_t user_flag, int fd);
        virtual         ~routine();

        static int64_t  gen_routine_id()
        {
                return  __sync_fetch_and_add(&s_unique_routine_id, 1);
        }

        void    close();

        void            set_user_data(void *data)
        {
                m_user_data = data;
        }
        void            set_user_flag(uint64_t flag)
        {
                m_user_flag = flag;
        }

        uint64_t	get_user_flag() const
        {
                return  m_user_flag;
        }

        void*           get_user_data()
        {
                return  m_user_data;
        }
        
	char		get_category() const
	{
		return	m_category;
	}

        uint16_t        get_proxy_id() const;

        uint64_t        get_routine_id() const
        {
                return        m_routine_id;
        }
        
        virtual int     inspect() = 0;

        /**
         * Used to return epoll events for epoll registering.
         */
        virtual        uint32_t        get_events() = 0;

        virtual        bool            vital() = 0;

        /**
         * Used to process epollin event.
         */
        virtual int        on_readable() = 0;

        /**
         * Used to process epollout event.
         */
        virtual int        on_writable() = 0;

        /**
         * Used to process epollerr event, which is possibly triggered
         * by networking failure and so on.
         */
        virtual void        on_error() = 0;

        /**
         * Used to process epollrdhup event, which is offten triggered by
         * remote peer shut down writing half of connection and so on.
         */
        virtual void        on_read_hup() = 0;

        /**
         * Used to process epollhup event, which is offten triggered 
         * by crushing of remote peer and so on.
         */
        virtual void        on_peer_hup() = 0;

	uint16_t	    get_port() const
	{
		return	(m_peer_addr.sin_port >> 8) + (m_peer_addr.sin_port & 0x00FF);
	}

protected:
        friend        class        routine_proxy;
        friend        class        service;
        friend        class        log_api;

        int                do_nonblock_write(const void *buf, unsigned len, unsigned &bytes_sent);
        int                do_nonblock_write();

	/**
	 * return value:
	 * > 0	received length
	 * = 0	not all received
	 * < 0	error occurred
	 */
        int                do_nonblock_recv(circular_buffer &cb);

        void        close_fd();

        virtual        bool        append_send_data(const void *data, unsigned len);

protected:
        static        signed long long        s_unique_routine_id;

	friend	class	ipc_routine;

	circular_buffer *m_send;
	circular_buffer *m_recv;
        routine_proxy       *m_proxy;
        uint64_t            m_create_time;
        uint64_t            m_touch_time;
        uint64_t            m_routine_id;
        char		m_category;
        bool            m_writable;
        bool            m_settled;
        int             m_fd;

        uint32_t        m_send_times;
        uint32_t        m_recv_times;
        uint32_t        m_send_bytes;
        uint32_t        m_recv_bytes;
        struct  sockaddr_in     m_peer_addr;
        void*           m_user_data;
        uint64_t	m_user_flag;
	uint64_t	m_last_inspect_time;
};
}


#endif //_HRTS_ROUTINE_H_
