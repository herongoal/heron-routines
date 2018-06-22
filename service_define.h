#ifndef _HRTS_SERVICE_DEFINE_H_
#define _HRTS_SERVICE_DEFINE_H_


#include <stdarg.h>
#include <cstdio>
#include <iostream>
using namespace std;
#include "service.h"


namespace hrts{
#define DEBUG_LOG	service::get_instance()->debug_log
#define FATAL_LOG	service::get_instance()->fatal_log
#define WARN_LOG	service::get_instance()->warn_log
#define ERROR_LOG	service::get_instance()->error_log
#define INFO_LOG	service::get_instance()->info_log
#define TRACE_LOG	service::get_instance()->trace_log

#define     REPORT_EVENT    report_event

void    report_event(const char *format, ...);
bool    test_byte_order();

extern  bool    g_big_edian;


const	uint16_t	ipc_msg_settle_routine = 1;
const	uint16_t	ipc_msg_transfer_data = 2;
const	uint16_t	ipc_msg_send_msg = 3;
const	uint16_t	ipc_msg_send_log = 4;
const	uint16_t	ipc_msg_stop_prx = 5;
const   uint16_t	ipc_msg_gen_report = 5;
const   uint16_t	ipc_msg_reload_config = 6;
const   uint16_t	ipc_msg_notify_stopped = 7;


struct  ipc_msg_header{
        ipc_msg_header(uint16_t type): msg_type(type),
		msg_size(0),
		dst_proxy_id(0),
		src_proxy_id(0),
		dst_routine_id(0),
		src_routine_id(0)
        {
                unique_msg_id = __sync_fetch_and_add(&next_unique_msg_id, 1);
		for(size_t n = 0; n < sizeof(int *); ++n) data[n] = 0;
        }

        static	uint64_t	next_unique_msg_id;
        uint64_t        	unique_msg_id;
        uint16_t        	msg_type;
        uint16_t        	msg_size;
        uint16_t        	dst_proxy_id;
        uint16_t        	src_proxy_id;
        uint64_t        	dst_routine_id;
        uint64_t        	src_routine_id;
	char			data[sizeof(int*)];
};//end of struct ipc_msg
}//end of namespace hrts


#endif //end of _HRTS_SERVICE_DEFINE_H_
