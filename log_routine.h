#ifndef _HRTS_LOG_ROUTINE_H_
#define _HRTS_LOG_ROUTINE_H_


#include "routine.h"


#include <linux/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <list>
#include <string>


namespace    hrts{
class    routine_proxy;


class    log_routine:public routine{
public:
    static    log_routine*    create(routine_proxy *proxy, char category, int fd);

    virtual    ~log_routine();

    virtual int    inspect();

    bool    append_send_data(const void *data, const unsigned data_len);

    /**
     * In parent class routine get_events is declared as pure virtual,
     * implement it to return epoll events for epoll registering.
     */
    virtual    uint32_t    get_events();

    virtual bool            vital(){return  true;}

    /**
     * In parent class routine on_readable is declared as pure virtual,
     * implement it to process epollin event.
     */
    virtual int    on_readable();

    /**
     * In parent class routine on_writable is declared as pure virtual,
     * implement it to process epollout event.
     */
    virtual int    on_writable();

    /**
     * In parent class routine on_error is declared as pure virtual,
     * implement it to process epollerr event.
     */
    virtual void    on_error();

    /**
     * In parent class routine on_read_hup is declared as pure virtual,
     * implement it to process epollrdhup event, for log_routine
     * this will never happen.
     */
    virtual void    on_read_hup();

    /**
     * In parent class routine on_peer_hup is declared as pure virtual,
     * implement it to process epollhup event, for log_routine
     * this will never happen.
     */
    virtual void    on_peer_hup();

private:
    std::list<std::string>    m_send_list;
    log_routine(routine_proxy *proxy, char category, int fd);
};
}


#endif //_HRTS_LOG_ROUTINE_H_
