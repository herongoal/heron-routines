#include "active_udp_session_routine.h"


#include "udp_define.h"
#include "circular_buffer.h"
#include "log_api.h"
#include "routine_proxy.h"
#include "service.h"


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>


#include <cstring>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>


using namespace std;
using std::vector;
using std::string;


namespace hrts{
active_udp_session_routine::active_udp_session_routine(routine_proxy *proxy, char category, uint64_t user_flag, int fd):
		routine(proxy, category, user_flag, fd),
		m_conversation_id(user_flag),
		m_peer_next_recv_seq(0),
		m_next_send_seq(0),
		m_peer_recv_window(0),
		m_recv_wait_seq(0),
		m_inspect_peer_time(0),
		m_peer_respond_time(0),
		m_inspect_times(0),
		m_inspect_timeout_times(0),
		m_last_resp_time(0),
		
		m_last_sync_time(0),
		m_forward_required(false),
		m_conv_window(64),
		m_awful_behaviors(0),
		m_ack_times(0),
		m_bad_ack_times(0),
		m_extra_ack_times(0),
		m_session_conv(0),
		m_total_transfer_rtt(0),
		m_max_transfer_rtt(0),
		m_transfer_times(0)
{
	m_writable = false;
}

active_udp_session_routine*	active_udp_session_routine::create(routine_proxy *proxy,
		int category, uint64_t user_flag,
		const char *ipaddr, uint16_t port)
{
	log_api	*logger = proxy->get_log_api();
	int	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd < 0)
	{
		logger->log(log_error, "active_udp_session_routine.socket errno=%d,errmsg=%s",
				errno, strerror(errno));
		return	NULL;
	}

	if(-1 == fcntl(fd, F_SETFL, O_NONBLOCK))
	{
		logger->log(log_error, "active_udp_session_routine.fcntl errno=%d,errmsg=%s",
				errno, strerror(errno));
		::close(fd);
		return	NULL;
	}

	active_udp_session_routine *rt = new active_udp_session_routine(proxy, category, user_flag, fd);
	if(NULL == rt)
	{
		::close(fd);
		return	NULL;
	}

	rt->m_conversation_id = user_flag;
	bzero(&rt->m_peer_addr, sizeof(rt->m_peer_addr));

	if(0 == inet_aton(ipaddr, &rt->m_peer_addr.sin_addr))
	{
		logger->log(log_error, "udp_service_routine.create invalid ipaddr=%s", ipaddr);
	}

	rt->m_peer_addr.sin_family = AF_INET;
	rt->m_peer_addr.sin_port = htons(port);

	size_t  bytes_num = 0;
        char    bytes[64];
        
        uint64_t       conv = convert_byte_order_u64(rt->m_conversation_id);
        uint8_t        frag_type = fragment_type_set_session;
        uint32_t       ts = rt->m_proxy->gen_monotonic_ms();
        
        memcpy(bytes + bytes_num, &conv, sizeof(conv));
        bytes_num += sizeof(conv);
        
        memcpy(bytes + bytes_num, &frag_type, sizeof(frag_type));
        bytes_num += sizeof(frag_type);
        
        memcpy(bytes + bytes_num, &ts, sizeof(ts));
        bytes_num += sizeof(ts);

        rt->do_nonblock_send(bytes, bytes_num);

	return	rt;
}

uint32_t	active_udp_session_routine::get_events()
{
	return	EPOLLIN;
}

active_udp_session_routine::~active_udp_session_routine()
{
	m_proxy->get_log_api()->log(this, log_trace, "~active_udp_session_routine,m_fd=%d,"
			"m_conversation_id=%lu,m_peer_next_recv_seq=%u,m_next_send_seq=%u,"
			"m_peer_recv_window=%u,m_recv_wait_seq=%u"
			"m_inspect_peer_time=%ld,m_peer_respond_time=%ld"
			"m_last_resp_time=%ld,m_last_sync_time=%ld,m_awful_behaviors=%d",
			m_fd, m_conversation_id, m_peer_next_recv_seq, m_next_send_seq,
			m_peer_recv_window, m_recv_wait_seq,
			m_inspect_peer_time, m_peer_respond_time,
			m_last_resp_time, m_last_sync_time, m_awful_behaviors);

	m_proxy->get_log_api()->log(this, log_trace, "~active_udp_session_routine,m_total_transfer_rtt=%lu,"
			"m_max_transfer_rtt=%u,m_transfer_times=%lu", m_total_transfer_rtt,
			m_max_transfer_rtt, m_transfer_times);

	m_send_fragment.clear();
	m_recv_fragment.clear();

	m_fd = -1;
}

int		active_udp_session_routine::inspect()
{
	const int64_t 	tnow = m_proxy->current_utc_ms();

	if(!m_writable)
	{
		size_t  bytes_num = 0;
		char    bytes[64];

		uint64_t       conv = convert_byte_order_u64(m_conversation_id);
		uint8_t        frag_type = fragment_type_set_session;
		uint32_t       ts = m_proxy->gen_monotonic_ms();

		memcpy(bytes + bytes_num, &conv, sizeof(conv));
		bytes_num += sizeof(conv);

		memcpy(bytes + bytes_num, &frag_type, sizeof(frag_type));
		bytes_num += sizeof(frag_type);

		memcpy(bytes + bytes_num, &ts, sizeof(ts));
		bytes_num += sizeof(ts);

		do_nonblock_send(bytes, bytes_num);
		return	0;
	}

	if(0 != forward(tnow))
	{
		m_fd = -1;
		return	-1;
	}

	return	0;
}

bool	active_udp_session_routine::dispose_ack(const fragment_header *sfh)
{
	if(sfh->seq >= m_next_send_seq)
	{
		m_proxy->get_log_api()->log(this, log_trace, "dispose_ack seq=%u,frag=%u,len=%u",
				sfh->seq, (unsigned)(unsigned char)sfh->frag, sfh->len);
		++m_bad_ack_times;
		return	false;
	}

	for(list<fragment_info>::iterator iter = m_send_fragment.begin();
			iter != m_send_fragment.end(); )
	{
		fragment_info &pfi = *iter;
		if(pfi.seq == sfh->seq)
		{
			++m_ack_times;
			service_msg_header *hdr = (service_msg_header *)pfi.data.data();

			if(pfi.attempt == 1)
			{
				unsigned transfer_rtt = m_proxy->gen_monotonic_ms() - pfi.send_time;
				if(transfer_rtt > m_max_transfer_rtt)
				{
					m_max_transfer_rtt = transfer_rtt;
				}
				m_total_transfer_rtt += transfer_rtt;
				++m_transfer_times;
			}

			m_proxy->get_log_api()->log(this, log_trace, "dispose_ack seq=%u,attempt=%u,elapse=%lu,msg_size=%u,msg_type=%u",
					sfh->seq, pfi.attempt, m_proxy->current_utc_ms() - pfi.send_time,
					hdr->msg_size, hdr->msg_type);
			string s;
			const char *ss = (const char *)&pfi.header;
			for(size_t n = 0; n < pfi.data.size(); ++n)
			{
				const char digits[] = "0123456789abcdef";
				unsigned char c = ss[n];
				char h = digits[c / 16];
				char l = digits[c % 16];
				s.append(1, h);
				s.append(1, l);
			}
			m_proxy->get_log_api()->log(this, log_trace, "dispose_ack seq=%u,%s", sfh->seq, s.c_str());
			iter = m_send_fragment.erase(iter);
			return	true;
		}
		else if(sfh->seq < pfi.seq)
		{
			m_proxy->get_log_api()->log(this, log_trace, "dispose_ack dropped seq=%u,ts=%u", sfh->seq, sfh->ts);
			++m_extra_ack_times;
			break;
		}
		else
		{
			++iter;
		}
	}
	return	false;
}

int	active_udp_session_routine::forward(uint64_t tnow)
{
	if(m_forward_required || !m_send_fragment.empty() || !m_sending_ack.empty())
	{
		uint16_t	wind = 0;
		if(m_conv_window > m_recv_fragment.size())
		{
			wind = m_conv_window - m_recv_fragment.size();
		}

		list<fragment_info>::iterator piter = m_send_fragment.begin();
		char	pack_data[1500];
		size_t	pack_len = 0;
		size_t	times = 0;

		while(m_send_fragment.end() != piter || !m_sending_ack.empty())
		{
			bool	deny_push = false;
			bool	deny_ack = false;

			for(;piter != m_send_fragment.end(); ++piter)
			{
				fragment_info &fi = *piter;
				if(pack_len + fi.data.size() + sizeof(fi.header) > gc_mss)
				{
					deny_push = true;
					break;
				}

				if(times > m_peer_recv_window)
				{
					continue;
				}

				++times;
				if(!fi.urgent && fi.send_time + 30 * fi.attempt > tnow)
				{
					continue;
				}

				if(fi.send_time == 0)
					fi.send_time = m_proxy->current_utc_ms();

				fi.urgent = false;
				++fi.attempt;

				fi.header.wind = convert_byte_order_u16(wind);

				memcpy(pack_data + pack_len, &fi.header, sizeof(fi.header));
				pack_len += sizeof(fi.header);
				memcpy(pack_data + pack_len, fi.data.data(), fi.data.size());
				pack_len += fi.data.size();
			}

			for(map<uint32_t,uint32_t>::iterator aiter = m_sending_ack.begin();
					aiter != m_sending_ack.end();)
			{
				if(pack_len + sizeof(fragment_header) > gc_mss)
				{
					deny_ack = true;
					break;
				}

				fragment_header	fh(m_conversation_id, fragment_type_ack, 0,
						wind, aiter->second,
						aiter->first,
						m_recv_wait_seq, 0);

				fh.len = 0;
				fh.network_byte_order();

				memcpy(pack_data + pack_len, &fh, sizeof(fh));
				pack_len += sizeof(fh);
				m_sending_ack.erase(aiter++);
			}

			if(0 == pack_len) break;

			if((piter == m_send_fragment.end() && m_sending_ack.empty())
					|| deny_push || deny_ack)
			{
				int ret = do_nonblock_send(pack_data, pack_len);
				if(ret > 0)
				{
					pack_len = 0;
				}
				else if(ret == 0)
				{
					m_proxy->get_log_api()->log(this, log_trace, "break....due to would block");
					break;
				}
				else
				{
					m_fd = -1;
					return	-1;
				}
			}
		}

	}

	const uint64_t	conv = convert_byte_order_u64(m_conversation_id);

	for(list<uint32_t>::iterator rr = m_alive_resp.begin();
			rr != m_alive_resp.end(); ++rr)
	{
		const uint32_t	ts = convert_byte_order_u32(*rr);
		char	buff[32];
		memcpy(buff, &conv, sizeof(conv));
		buff[sizeof(conv)] = fragment_type_ack_alive;
		memcpy(buff + sizeof(conv) + 1, &ts, sizeof(ts));
		do_nonblock_send(buff, sizeof(conv) + 1 + sizeof(ts));
	}

	m_alive_resp.clear();


	for(list<uint32_t>::iterator rr = m_session_resp.begin();
			rr != m_session_resp.end(); ++rr)
	{
		const uint32_t	ts = convert_byte_order_u32(*rr);
		char	buff[32];
		memcpy(buff, &conv, sizeof(conv));
		buff[sizeof(conv)] = fragment_type_ack_session;
		memcpy(buff + sizeof(conv) + 1, &ts, sizeof(ts));
		do_nonblock_send(buff, sizeof(conv) + 1 + sizeof(ts));
	}
	m_session_resp.clear();

	return	0;
}

bool	active_udp_session_routine::process_peer_nrs(unsigned cmd, uint32_t seq, uint32_t nrs)
{
	if(nrs < m_peer_next_recv_seq)
	{
		return	false;
	}

	m_proxy->get_log_api()->log(this, log_trace, "process_peer_nrs,m_peer_next_recv_seq=%u,cmd=%d,seq=%u,nrs=%u",
			m_peer_next_recv_seq, cmd, seq, nrs);
	m_peer_next_recv_seq = nrs;

	bool urgent = false;
	for(list<fragment_info>::iterator iter = m_send_fragment.begin();
			iter != m_send_fragment.end();)
	{
		fragment_info &fi = *iter;
		if(fi.seq < m_peer_next_recv_seq)
		{
			m_proxy->get_log_api()->log(this, log_trace, 
					"process_peer_nrs,finished fi.seq=%u,fi.frag_len=%u,fi.frag=%u",
					fi.seq, fi.data.size(),
					(unsigned)(unsigned char)fi.header.frag);
			iter = m_send_fragment.erase(iter);
		}
		else if(fi.seq == m_peer_next_recv_seq)
		{
			m_proxy->get_log_api()->log(this, log_trace, "process_peer_nrs,set fi.seq=%u urgent",
					fi.seq);
			urgent = fi.urgent = true;
			++iter;
		}
		else
		{
			break;
		}
	}

	return	urgent;
}


int	active_udp_session_routine::dispose_push(const fragment_header *fh,
		const void *data, unsigned len)
{
	m_proxy->get_log_api()->log(this, log_trace, "dispose_push,"
			"m_recv_wait_seq=%u,m_conv_window=%u,seq=%u",
			m_recv_wait_seq, m_conv_window, fh->seq);

	if (fh->seq >= m_recv_wait_seq + m_conv_window)
	{
		m_proxy->get_log_api()->log(this, log_trace, "dispose_push.illegal:drop "
				"m_recv_wait_seq=%u,m_conv_window=%u,seq=%u",
				m_recv_wait_seq, m_conv_window, fh->seq);

		++m_awful_behaviors;
		return	0;
	}
	else if (fh->seq < m_recv_wait_seq)
	{
		m_sending_ack[fh->seq] = fh->ts;
		m_forward_required = true;
		m_proxy->get_log_api()->log(this, log_trace, "dispose_push.repeated:drop "
				"m_recv_wait_seq=%u,m_conv_window=%u,seq=%u",
				m_recv_wait_seq, m_conv_window, fh->seq);

		return	0;
	}

	m_sending_ack[fh->seq] = fh->ts;
	m_forward_required = true;

	list<fragment_info>::iterator iter = m_recv_fragment.begin();
	while(iter != m_recv_fragment.end())
	{
		const fragment_info pp = *iter;
		if(fh->seq == pp.seq)
		{
			return	0;
		}
		else if(fh->seq > pp.seq)
		{
			++iter;
		}
		else
		{
			//++iter;
			break;
		}
	}

	fragment_info fi(*fh, data, fh->len);
	m_recv_fragment.insert(iter, fi);

	iter = m_recv_fragment.begin();
	while(iter != m_recv_fragment.end() && (*iter).header.seq == m_recv_wait_seq)
	{
		fragment_info	fi = *iter;
		if(!m_recv->append(fi.data.data(), fi.data.size()))
		{
			m_proxy->get_log_api()->log(this, log_error, "dispose_push.append failed");
			return	false;
		}

		m_recv_wait_seq++;
		if(fi.header.frag == 0)
		{
			service::get_instance()->process(m_proxy, this, m_recv);
		}

		iter = m_recv_fragment.erase(iter);
	}

	return	0;
}

int     active_udp_session_routine::on_writable()
{       
	m_proxy->get_log_api()->log(this, log_trace, "on_writable,nothing to do");
	return	0;
}

int	active_udp_session_routine::on_readable()
{
	static  const   int     socket_buffer_size = 65535;
	static  const   int     min_pack_len = sizeof(keep_alive_msg);
        static  const   int     flag = MSG_DONTWAIT;
        struct  sockaddr_in     cli_addr;

        char    data[65535];
        int     bytes_read = 0;

        while(bytes_read < socket_buffer_size)
        {
                socklen_t       cli_len = sizeof(cli_addr);
                int recv_len = recvfrom(m_fd, data, sizeof(data), flag, (struct sockaddr*)&cli_addr, &cli_len);

                if(-1 == recv_len)
                {
                        if(errno == EAGAIN || errno == EWOULDBLOCK || EINTR == errno)
                        {
                                m_proxy->get_log_api()->log(this, log_all, "on_readable.recvfrom, all read");
                                break;
                        }
                        else
                        {
                                m_proxy->get_log_api()->log(this, log_error, "on_readable.recvfrom, %d occurred", errno);
                                return  -1;
                        }
                }

                bytes_read += recv_len;
                if(recv_len < min_pack_len)
                {
                        m_proxy->get_log_api()->log(this, log_trace, "on_readable.recv_len=%u", min_pack_len);
                        continue;
                }

                fragment_header header;
                unsigned pos = 0;
                header.read_conversation(data, recv_len, pos);
                header.read_frag_type(data, recv_len, pos);

                if(0 != dispose(data, recv_len))
                {
                        m_proxy->get_log_api()->log(this, log_error, "dispose %u bytes error occurred.", recv_len);
                        service::get_instance()->on_routine_closed(this);
                        m_proxy->close_routine(m_routine_id);
                }
        }
	return	0;
}

int     active_udp_session_routine::dispose(const char *data, const unsigned len)
{
        unsigned        read_bytes = 0;
        fragment_header header;

        while(read_bytes < len)
        {
                if(!header.read_conversation(data, len, read_bytes)
                        || !header.read_frag_type(data, len, read_bytes))
                {
                        m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing"
                                        " header.prefix after %u of %u bytes",
                                        read_bytes, len);
			return	0;
                }

                if(header.frag_type == fragment_type_set_session)
                {
                        //ts should be examined .
                        if(!header.read_ts(data, len, read_bytes))
                        {
                                m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing"
                                        " header.ts after %u of %u bytes",
                                        read_bytes, len);
				return	0;
                        }

                        m_session_resp.push_back(header.ts);
                        m_forward_required = true;
                        continue;
                }
                else if(header.frag_type == fragment_type_ack_session)
                {
                        if(!header.read_ts(data, len, read_bytes))
                        {
                                m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing"
                                        " header.ts after %u of %u bytes",
                                        read_bytes, len);
                                return	0;
                        }
			m_writable = true;
                        continue;
                }

                if(header.frag_type == fragment_type_keep_alive
                        || header.frag_type == fragment_type_ack_alive)
                {
                        //ts should be examined .
                        if(!header.read_ts(data, len, read_bytes))
                        {
                                m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing"
                                        " header.ts after %u of %u bytes",
                                        read_bytes, len);
				return	0;
                        }
                        else if(header.frag_type == fragment_type_keep_alive)
                        {
                                m_alive_resp.push_back(header.ts);
                                m_inspect_timeout_times = 0;
                                m_forward_required = true;
                        }
                        else if(m_inspect_peer_time > 0)
                        {
                                int64_t tnow = m_proxy->current_utc_ms();
                                //should be optimized here. ts should be validated
                                if(tnow <= m_inspect_peer_time + m_timeout_ms)
                                {
                                        m_peer_respond_time = tnow;
                                        m_inspect_timeout_times = 0;
                                }
                                else
                                {
                                        ++m_inspect_timeout_times;
                                }
                        }

                        continue;
                }

                if(!header.read_extra(data, len, read_bytes))
                {
                        break;
                }

                if(read_bytes + header.len > len)
                {
                        m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing "
                                        "data of header.frag_type=%u,header.frag=%u,header.wind=%u,"
                                        "header.ts=%u,header.seq=%u,header.naf=%u,header.len=%u "
                                        "after %u of %u bytes",
                                        (unsigned char)header.frag_type,
                                        (unsigned char)header.frag,
                                        header.wind,
                                        header.ts,
                                        header.seq,
                                        header.nrs,
                                        header.len,
                                        read_bytes, len);
                        return  0;
                }

                if(fragment_type_push == header.frag_type)
                {
                        //if(process_peer_nrs(header.frag_type, header.seq, header.nrs))
                        {
                                m_forward_required = true;
                        }
                        dispose_push(&header, data + read_bytes, header.len);
                        read_bytes += header.len;
                }
                else    if(fragment_type_ack == header.frag_type)
                {
                        dispose_ack(&header);
                        m_peer_recv_window = header.wind;
                }
                else
                {
                        m_proxy->get_log_api()->log(this, log_trace, "dispose unexpected cmd=%u",
                                (unsigned char)header.frag_type);
                        break;
                }
        }

        if(len - read_bytes != 0)
        {
                m_proxy->get_log_api()->log(this, log_trace, "dispose unexpected data,read_bytes=%u,len=%u",
                                read_bytes, len);
                return  0;
        }

        forward(m_proxy->current_utc_ms());
        return  0;
}

int     active_udp_session_routine::do_nonblock_send(const void *data, unsigned len)
{
	static const int flag = MSG_DONTWAIT;

	const int ret = sendto(m_fd, data, len, flag, (const struct sockaddr *)&m_peer_addr,
			sizeof(m_peer_addr));

	m_proxy->get_log_api()->log(this, log_debug, "do_nonblock_send.sendto, m_fd=%d,len=%u,port=%u,tom=%s",
			m_fd, len, m_peer_addr.sin_port, "tom");

	if(ret == (int)len)
	{
		m_last_sync_time = m_proxy->current_utc_ms();
		return	1;
	}
	else if(ret < 0 && (EAGAIN == errno || EWOULDBLOCK == errno))
	{
		m_proxy->get_log_api()->log(this, log_warn, "do_nonblock_send.send not success");
		return  0;
	}
	else
	{
		m_proxy->get_log_api()->log(this, log_error, "do_nonblock_send.sendto, errno=%d,errmsg=%s occurred",
				errno, strerror(errno));
		m_fd = -1;
		return	-1;
	}
}

void	active_udp_session_routine::on_error()
{
	m_proxy->get_log_api()->log(this, log_info, "on_error,%d occurred", errno);
}

void	active_udp_session_routine::on_read_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_read_hup was triggered");
}

void	active_udp_session_routine::on_peer_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_peer_hup was triggered");
}

bool    active_udp_session_routine::append_send_data(const void *data, const unsigned len)
{
	const unsigned	max_data = gc_mss - sizeof(fragment_header) - 500;
	unsigned	fra_amount = len / max_data;
	unsigned	append_len = 0u;

	const char *p = (const char *)data;

	if(len % max_data != 0u) ++fra_amount;

	while(fra_amount-- > 0u)
	{
		fragment_info sf;
		sf.seq = sf.header.seq = m_next_send_seq++;
		sf.header.len = len - append_len;

		if(sf.header.len > max_data) sf.header.len = max_data;

		sf.data.append(p + append_len, sf.header.len);
		append_len += sf.header.len;
		m_send_bytes += sf.header.len;
		++m_send_times;

		sf.header.conversation = m_conversation_id;
		sf.header.frag_type = fragment_type_push;
		sf.header.frag = fra_amount;
		sf.header.ts = m_proxy->current_utc_ms();
		sf.header.nrs = m_recv_wait_seq;

		sf.header.network_byte_order();
		m_send_fragment.push_back(sf);
	}

	m_proxy->get_log_api()->log(this, log_trace, "append_send_data, len=%u, m_next_send_seq=%u",
			len, m_next_send_seq);
	m_forward_required = true;
	forward(m_proxy->gen_monotonic_ms());
	return  true;
}
}
