#include "udp_define.h"


#include <cstring>


namespace	hrts{
uint16_t convert_byte_order_u16(uint16_t val)
{
        uint16_t ret = 0;
        for(unsigned n = 0; n < sizeof(uint16_t); ++n)
        {
                ret = (ret << 8) + (val & 0xFF);
                val = val >> 8;
        }

        return  ret;
}

uint32_t convert_byte_order_u32(uint32_t val)
{
        uint32_t ret = 0;
        for(unsigned n = 0; n < sizeof(uint32_t); ++n)
        {
                ret = (ret << 8) + (val & 0xFF);
                val = val >> 8;
        }

        return  ret;
}

uint64_t convert_byte_order_u64(uint64_t val)
{
        uint64_t ret = 0;
        for(unsigned n = 0; n < sizeof(uint64_t); ++n)
        {
                ret = (ret << 8) + (val & 0xFF);
                val = val >> 8;
        }

        return  ret;
}

void    fragment_header::network_byte_order()
{
	if(!g_big_edian)
	{
                conversation = convert_byte_order_u64(conversation);
                wind = convert_byte_order_u16(wind);
                ts = convert_byte_order_u32(ts);
                seq = convert_byte_order_u32(seq);
                nrs = convert_byte_order_u32(nrs);
                len = convert_byte_order_u32(len);
	}
}

fragment_header::fragment_header(const void *data)
{
	memcpy(this, data, sizeof(*this));
	host_byte_order();
}

void    fragment_header::host_byte_order()
{
	if(!g_big_edian)
	{
                conversation = convert_byte_order_u64(conversation);
                wind = convert_byte_order_u16(wind);
                ts = convert_byte_order_u32(ts);
                seq = convert_byte_order_u32(seq);
                nrs = convert_byte_order_u32(nrs);
                len = convert_byte_order_u32(len);
	}
}

bool    fragment_header::read_frag_type(const void* data, unsigned data_len,
        unsigned& pos)
{
        if(pos + sizeof(frag_type) > data_len)
        {
                return false;
        }

        
        const   uint8_t*  p_bytes = (const uint8_t *)data + pos;

        frag_type = p_bytes[0];
        pos += sizeof(frag_type);

        return  true;
}

bool    fragment_header::read_ts(const void* data, unsigned data_len,
        unsigned& pos)
{
        if(pos + sizeof(ts) > data_len)
        {
                return false;
        }

        const   uint8_t*  p_bytes = (const uint8_t *)data + pos;
	ts = 0;

	for(size_t n = 0; n < sizeof(ts); ++n)
	{
		ts = (ts << 8) + p_bytes[n];
	}

	pos += sizeof(ts);
	return  true;
}

bool    fragment_header::read_conversation(const void* data, unsigned data_len,
        unsigned& pos)
{
        if(pos + sizeof(conversation) > data_len)
        {
                return false;
        }

        const   uint8_t*  p_bytes = (const uint8_t *)data + pos;
	conversation = 0;

	for(size_t n = 0; n < sizeof(conversation); ++n)
	{
		conversation = (conversation << 8) + p_bytes[n];
	}

	pos += sizeof(conversation);
	return  true;
}

bool    fragment_header::read_extra(const void* data, unsigned data_len,
        unsigned& pos)
{
        const int extra_len = sizeof(frag) + sizeof(wind) + sizeof(ts) + sizeof(seq) + sizeof(nrs) + sizeof(len);
        const   uint8_t*  p_bytes;

        if(pos + extra_len > data_len)
        {
                return false;
        }

        p_bytes = (const uint8_t *)data + pos;
        frag = p_bytes[0];
        pos += sizeof(frag);

        p_bytes = (const uint8_t *)data + pos;
        wind = 0;
        for(size_t n = 0; n < sizeof(wind); ++n)
	{
		wind = (wind << 8) + p_bytes[n];
	}
        pos += sizeof(wind);

        p_bytes = (const uint8_t *)data + pos;
        ts = 0;
        for(size_t n = 0; n < sizeof(ts); ++n)
	{
		ts = (ts << 8) + p_bytes[n];
	}
        pos += sizeof(ts);

        p_bytes = (const uint8_t *)data + pos;
        seq = 0;
        for(size_t n = 0; n < sizeof(seq); ++n)
	{
		seq = (seq << 8) + p_bytes[n];
	}
        pos += sizeof(seq);

        p_bytes = (const uint8_t *)data + pos;
        nrs = 0;
        for(size_t n = 0; n < sizeof(nrs); ++n)
	{
		nrs = (nrs << 8) + p_bytes[n];
	}
        pos += sizeof(nrs);

        p_bytes = (const uint8_t *)data + pos;
        len = 0;
        for(size_t n = 0; n < sizeof(len); ++n)
	{
		len = (len << 8) + p_bytes[n];
	}
        pos += sizeof(len);
	return  true;
}

fragment_info::fragment_info(const fragment_header &fh, const void *body, unsigned len):
	header(fh), urgent(true)
{
	data.assign((const char *)body, len);
	seq = header.seq;
}
}
