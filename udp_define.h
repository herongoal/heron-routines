/*******************************************************************************
 * 1. 全双工的
 * 2. 快速的
 * 3. 安全的(不能我发了别人没收到，我就疯狂地发，带有窗口的)
 * 4. 高效的(一次确认多个包) 
 ******************************************************************************/


#ifndef _HRTS_UDP_DEFINE_H_
#define _HRTS_UDP_DEFINE_H_


#include "service_define.h"


#include <stdint.h>
#include <iostream>


using namespace std;


namespace       hrts{
uint16_t convert_byte_order_u16(uint16_t val);

uint32_t convert_byte_order_u32(uint32_t val);

uint64_t convert_byte_order_u64(uint64_t val);

static	const	size_t	max_recv_wind = 32;

enum	fragment_type{
	fragment_type_push = 81,
	fragment_type_ack = 82,
	fragment_type_wask = 83,
	fragment_type_wins = 84,
	fragment_type_keep_alive = 85,
	fragment_type_ack_alive = 86,
	fragment_type_set_session = 87,
	fragment_type_ack_session = 88,
};

#pragma pack(1)
struct  fragment_header{
	fragment_header(uint64_t conv, uint8_t fragment_type, uint8_t fragment,
			uint16_t window, uint32_t timestamp,
			uint32_t sequence,
			uint32_t next_recv_seq,
			uint32_t frag_len):
		conversation(conv),
		frag_type(fragment_type),
		frag(fragment),
		wind(window),
		ts(timestamp),
		seq(sequence),
		nrs(next_recv_seq),
		len(frag_len)
	{
	}

	fragment_header&	operator=(const fragment_header &fh)
	{
		this->conversation = fh.conversation;
                this->frag_type = fh.frag_type;
                this->frag = fh.frag;
                this->wind = fh.wind;
                this->ts = fh.ts;
                this->seq = fh.seq;
                this->nrs = fh.nrs;
                this->len = fh.len;
		return	*this;
	}

	fragment_header(const fragment_header &fh)
	{
		*this = fh;
	}

	fragment_header(const void *data);
	fragment_header(): conversation(0),
		frag_type(0),
		frag(0),
		wind(0),
		ts(0),
		seq(0),
		nrs(0),
		len(0)
	{
	}

	bool	read_conversation(const void* data, unsigned data_len,
			unsigned& pos);
	bool	read_ts(const void* data, unsigned data_len,
			unsigned& pos);
	bool	read_frag_type(const void* data, unsigned data_len,
			unsigned& pos);

	bool	read_extra(const void* data, unsigned data_len,
			unsigned& pos);

	void    	network_byte_order();
	void    	host_byte_order();

	uint64_t       conversation;
	uint8_t        frag_type;
	uint8_t        frag;
	uint16_t       wind;
	uint32_t       ts;
	uint32_t       seq;
	uint32_t       nrs;
	uint32_t       len;
};
#pragma pack()


#pragma pack(1)
struct  keep_alive_msg{
        keep_alive_msg(uint64_t conv, uint8_t fragment_type, uint32_t timestamp):
                conversation(conv),
                frag_type(fragment_type),
                ts(timestamp)
        {
        }

        keep_alive_msg&        operator=(const keep_alive_msg &fh)
        {
                this->conversation = fh.conversation;
                this->frag_type = fh.frag_type;
                this->ts = fh.ts;
                return  *this;
        }

        keep_alive_msg(const keep_alive_msg &fh)
        {
                *this = fh;
        }

        keep_alive_msg(const void *data);

        void            network_byte_order();
        void            host_byte_order();

        uint64_t       conversation;
        uint8_t        frag_type;
        uint32_t       ts;
};
#pragma pack()

/**
 * MSS defined for internet in RFC is 576, but IP header occupies 20 bytes,
 * UDP header occupies 8 bytes, and session header occupies 20 bytes,
 * so 528 bytes is left.
 */
const 	unsigned	gc_mss = 576 - 20 - 8;

#pragma pack(1)
struct  fragment_info{
	fragment_info(const fragment_header &fh, const void *data, unsigned len);
	fragment_info():header(0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u),
			attempt(0), seq(0), send_time(0), urgent(true)
	{
	}

	fragment_info(const fragment_info &fi)
	{
		*this = fi;
	}

	fragment_info&	operator=(const fragment_info &fi)
	{
		this->header = fi.header;
		this->data = fi.data;
		this->attempt = fi.attempt;
		this->seq = fi.seq;
		this->send_time = fi.send_time;
		this->urgent = fi.urgent;
		return	*this;
	}
	
	fragment_header		header;
	string			data;
        uint16_t        attempt;
        uint32_t        seq;
        uint64_t        send_time;
	bool		urgent; //should be used carefully
};
#pragma pack()
}//end of namepace hrts


#endif //end of _HRTS_UDP_DEFINE_H_
