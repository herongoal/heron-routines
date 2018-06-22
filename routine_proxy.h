/*******************************************************************************
 *
 *
 *******************************************************************************/
#ifndef    _HRTS_ROUTINE_PROXY_H_
#define    _HRTS_ROUTINE_PROXY_H_


#include "log_api.h"
#include "file_writer.h"
#include "timer.h"
#include "circular_buffer.h"
#include "ipc_routine.h"
#include "log_routine.h"
#include "hybrid_list.h"


#include <pthread.h>
#include <sys/epoll.h>
#include <stdarg.h>
#include <sys/time.h>

#include <iostream>
#include <stdint.h>
#include <string>
#include <map>
#include <list>

using namespace std;

namespace hrts{
class    service_routine;
class    routine;


using    std::list;
using    std::map;

class routine_proxy{
public:
    routine_proxy(unsigned proxy_id, bool auto_resume, log_level log_level, file_writer *log_writer);

    void    close_routine(uint64_t routine_id);
    bool    register_timer(const timer_info &ti);
    void	delete_timer(uint64_t register_data);
    void    	add_routine(routine* r);
    virtual 	~routine_proxy();

    uint16_t    get_proxy_id()    const {return    m_proxy_id;}

    routine 	*get_routine(uint64_t routine_id)
    {
	    return	(routine *)m_routine_mgr.search_elem(routine_id);
    }

    log_api *get_log_api(){ return m_log_api; }
    
    bool    transfer_msg(int8_t proxy_id, const void *data, unsigned len);
    bool    send_message(int8_t proxy_id, uint64_t routine_id, const void *data, unsigned len);

    void    send_log(const char *data, unsigned len);
    uint64_t    current_utc_ms();
    uint64_t    gen_monotonic_ms();

    void        modify_events(routine *rt);
    void    set_routine_data(uint64_t routine_id, void *data);
private:
    void*   stop_proxy_loop();

    bool    should_run(){return m_auto_resume;}
    void    process_timers();
    void    react();
    void    inspect();
    void    process_events(uint64_t routine_id, unsigned events);
    
    void    require_stop() { m_auto_resume = false; }
private:
    friend    class    udp_service_routine;
    static    void*    proxy_loop(void* arg);

    void        unregister_events(routine *rt);

    void        settle_routine(uint64_t routine_id, uint16_t proxy_id);

private:
    friend    class    service_routine;
    friend    class    service;
    friend    class    routine;
    friend    class    ipc_routine;

    int                              m_log_sockpair_fd[2];
    int                              m_ipc_sockpair_fd[2];
    log_routine*                     m_log_routine;
    ipc_routine*                     m_ipc_routine;
    file_writer*                     m_log_writer;

    hybrid_list                     m_routine_mgr;

    timer_manager                   m_sync_timers;
    timer_manager                   m_check_timers;

    elapse_time                       m_event_proc_elapse;
    
    int                               m_epoll_fd;
    uint16_t                          m_proxy_id;
    struct timespec                   m_timespec;

    log_api*                          m_log_api;
    bool                              m_auto_resume;
    pthread_t                         m_threadid;
};

}//namespace hrts


#endif //_HRTS_ROUTINE_PROXY_H_
