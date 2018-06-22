#include "udp_session_context.h"


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
udp_session_context::udp_session_context(routine_proxy *proxy, char category, int fd):
		routine(proxy, category, 0, fd),
		m_conversation_id(0),
		m_peer_next_recv_seq(0),
		m_next_send_seq(0),
		m_peer_recv_window(64),
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
	m_writable = true;
	bzero(&m_peer_addr, sizeof(m_peer_addr));
}

udp_session_context*	udp_session_context::create(routine_proxy *proxy, int routine_type, int sock_fd)
{
	/*
	int	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd < 0)
	{
		proxy->get_log_api()->log(log_error, "udp_session_context.socket errno=%d,errmsg=%s",
				errno, strerror(errno));
		return	NULL;
	}

	if(-1 == fcntl(fd, F_SETFL, O_NONBLOCK))
	{
		proxy->get_log_api()->log(log_error, "udp_session_context.fcntl[set NONBLOCK] fd=%d errno=%d",
				fd, errno);
		//not that serious an error, because NONBLOCK flag will be used while reading/writing.
	}
	*/

	proxy->get_log_api()->log(log_trace, "create_udp_session_context,fd=%d", sock_fd);
	return	new udp_session_context(proxy, routine_type, sock_fd);
}

uint32_t	udp_session_context::get_events()
{
	return	0;
}

udp_session_context::~udp_session_context()
{
	m_proxy->get_log_api()->log(this, log_trace, "~udp_session_context,m_fd=%d,"
			"m_conversation_id=%lu,m_peer_next_recv_seq=%u,m_next_send_seq=%u,"
			"m_peer_recv_window=%u,m_recv_wait_seq=%u"
			"m_inspect_peer_time=%ld,m_peer_respond_time=%ld"
			"m_last_resp_time=%ld,m_last_sync_time=%ld,m_awful_behaviors=%d",
			m_fd, m_conversation_id, m_peer_next_recv_seq, m_next_send_seq,
			m_peer_recv_window, m_recv_wait_seq,
			m_inspect_peer_time, m_peer_respond_time,
			m_last_resp_time, m_last_sync_time, m_awful_behaviors);

	m_proxy->get_log_api()->log(this, log_trace, "~udp_session_context,m_total_transfer_rtt=%lu,"
			"m_max_transfer_rtt=%u,m_transfer_times=%lu", m_total_transfer_rtt,
			m_max_transfer_rtt, m_transfer_times);

	m_send_fragment.clear();
	m_recv_fragment.clear();

	m_fd = -1;
}

int		udp_session_context::inspect()
{
		const int64_t 	tnow = m_proxy->current_utc_ms();
		
		if(0 != forward(tnow))
		{
				m_fd = -1;
				return	-1;
		}

		/**
		 * keep_alive request has been sent, but response was
		 * not received yet.
		 */
		if(0 == m_peer_respond_time && m_inspect_peer_time > 0)
		{
			if(tnow > m_inspect_peer_time + m_timeout_ms)
			{
				m_proxy->get_log_api()->log(this, log_trace, "session_debug.inspect timeout:m_inspect_peer_time=%lu,"
							"tnow=%lu",
							m_inspect_peer_time, tnow);

				m_inspect_peer_time = 0;
				++m_inspect_times;
				++m_inspect_timeout_times;
			}
		}
		else
		{
			if(m_last_sync_time + m_timeout_ms < tnow
				&& m_last_resp_time + m_timeout_ms < tnow)
			{
				if(m_inspect_peer_time + m_timeout_ms < tnow)
				{
					m_inspect_peer_time = tnow;
					m_peer_respond_time = 0;

					char	dat[13];
					int64_t	conv = convert_byte_order_u64(m_conversation_id);
					memcpy(dat, &conv, sizeof(conv));
					dat[8] = 85;
					uint32_t ts = convert_byte_order_u32(m_inspect_peer_time);
					memcpy(dat + 9, &ts, sizeof(ts));
					do_nonblock_send(dat, sizeof(dat));
				}
			}
		}

		if(m_inspect_timeout_times > 7)
		{
			//m_fd = -1;
			//return	-1;
		}

		return	0;
}

bool	udp_session_context::dispose_ack(const fragment_header *sfh)
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

			unsigned transfer_rtt = (m_proxy->gen_monotonic_ms() - pfi.send_time);
			if(transfer_rtt > m_max_transfer_rtt)
			{
				m_max_transfer_rtt = transfer_rtt;
			}
			m_total_transfer_rtt += transfer_rtt;
			++m_transfer_times;
			m_push_srtt.push_back(transfer_rtt);
			if(m_push_srtt.size() > 5)
			{
				m_push_srtt.erase(m_push_srtt.begin());
			}

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
			m_proxy->get_log_api()->log(this, log_trace, "dispose_ack seq=%u,attempt=%u,elapse=%lu,msg_size=%u,msg_type=%u,wind=%u,msg=%s",
					sfh->seq, pfi.attempt, m_proxy->current_utc_ms() - pfi.send_time,
					hdr->msg_size, hdr->msg_type, sfh->wind, s.c_str());

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

int	udp_session_context::forward(uint64_t tnow)
{
	int stimeout = 0;

	if(m_push_srtt.empty())
	{
		stimeout = 60;
	}
	else
	{
		for(list<int>::const_iterator iter = m_push_srtt.begin();
				iter != m_push_srtt.end(); ++iter)
		{
			stimeout += *iter;
		}
		stimeout = stimeout / m_push_srtt.size();
	}

	if(stimeout > 80) stimeout = 80;

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

		while(m_send_fragment.end() != piter || !m_sending_ack.empty())
		{
			bool	deny_push = false;
			bool	deny_ack = false;

			while(piter != m_send_fragment.end())
			{
				fragment_info &fi = *piter;
				if(pack_len + fi.data.size() + sizeof(fi.header) > gc_mss)
				{
					deny_push = true;
					break;
				}

				if(fi.seq - m_send_fragment.begin()->seq + 1 > m_peer_recv_window)
				{
					piter = m_send_fragment.end();
					break;
				}

				if(!fi.urgent && fi.send_time + stimeout * fi.attempt > tnow)
				{
					++piter;
					continue;
				}
				++piter;

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

bool	udp_session_context::process_peer_nrs(unsigned cmd, uint32_t seq, uint32_t nrs)
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


int	udp_session_context::dispose_push(const fragment_header *fh,
		const void *data, unsigned len)
{
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
	m_proxy->get_log_api()->log(this, log_trace, "dispose_push.append seq=%u,m_recv_wait_seq=%u,frag=%u,fh->len=%u,len=%u",
			fi.seq, m_recv_wait_seq, fi.header.frag, fh->len, len);

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

int	udp_session_context::dispose(const char *data, const unsigned len)
{
	unsigned	read_bytes = 0;
	fragment_header	header;

	while(read_bytes < len)
	{
		if(!header.read_conversation(data, len, read_bytes)
			|| !header.read_frag_type(data, len, read_bytes))
		{
			m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing"
					" header.prefix after %u of %u bytes",
					read_bytes, len);
			m_fd = -1;
			return	-1;
		}

		if(header.frag_type == fragment_type_set_session)
		{
			//ts should be examined .
			if(!header.read_ts(data, len, read_bytes)) 
			{
				m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing"
					" header.ts after %u of %u bytes",
					read_bytes, len);
				m_fd = -1;
				return	-1;
			}

			m_session_resp.push_back(header.ts);
			m_forward_required = true;
			continue;
		}
		else if(header.frag_type == fragment_type_ack_session)
		{
			//ts should be examined .
			if(!header.read_ts(data, len, read_bytes)) 
			{
				m_proxy->get_log_api()->log(this, log_error, "dispose:not enough for parsing"
					" header.ts after %u of %u bytes",
					read_bytes, len);
				m_fd = -1;
				return	-1;
			}
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
				m_fd = -1;
				return	-1;
			}
			else if(header.frag_type == fragment_type_keep_alive)
			{
				m_alive_resp.push_back(header.ts);
				m_inspect_timeout_times = 0;
				m_forward_required = true;
			}
			else if(m_inspect_peer_time > 0)
			{
				int64_t	tnow = m_proxy->current_utc_ms();
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
			m_fd = -1;
			return	-1;
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
		else	if(fragment_type_ack == header.frag_type)
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
		m_fd = -1;
		return	-1;
	}

	forward(m_proxy->current_utc_ms());
	return	0;
}

int     udp_session_context::on_writable()
{       
	m_proxy->get_log_api()->log(this, log_trace, "on_writable,nothing to do");
	return	0;
}

int	udp_session_context::on_readable()
{
	m_proxy->get_log_api()->log(this, log_trace, "on_readable,nothing to do");
	return	0;
}

int     udp_session_context::do_nonblock_send(const void *data, unsigned len)
{
	static const int flag = MSG_DONTWAIT;

	const int ret = sendto(m_fd, data, len, flag, (const struct sockaddr *)&m_peer_addr,
			sizeof(m_peer_addr));

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
		m_proxy->get_log_api()->log(this, log_error, "do_nonblock_send.sendto, %d occurred", errno);
		m_fd = -1;
		return	-1;
	}
}

void	udp_session_context::on_error()
{
	m_proxy->get_log_api()->log(this, log_info, "on_error,%d occurred", errno);
}

void	udp_session_context::on_read_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_read_hup was triggered");
}

void	udp_session_context::on_peer_hup()
{
	m_proxy->get_log_api()->log(this, log_all, "on_peer_hup was triggered");
}

bool    udp_session_context::append_send_data(const void *data, const unsigned len)
{
	const unsigned	max_data = gc_mss - sizeof(fragment_header);
	unsigned	fra_amount = len / max_data;
	unsigned	append_len = 0u;

	const char *p = (const char *)data;

	if(len % max_data != 0u) ++fra_amount;

	while(fra_amount-- > 0u)
	{
		fragment_info sf;
		sf.seq = sf.header.seq = m_next_send_seq++;
		sf.header.len = len - append_len;
		sf.header.frag = fra_amount;

		if(sf.header.len > max_data) sf.header.len = max_data;

		m_proxy->get_log_api()->log(this, log_trace, "append_send_data,seq=%u,frag=%u,len=%u",
				sf.seq, sf.header.frag, sf.header.len);

		sf.data.append(p + append_len, sf.header.len);
		append_len += sf.header.len;
		m_send_bytes += sf.header.len;
		++m_send_times;

		sf.header.conversation = m_conversation_id;
		sf.header.frag_type = fragment_type_push;
		sf.header.ts = m_proxy->current_utc_ms();
		sf.header.nrs = m_recv_wait_seq;

		sf.header.network_byte_order();
		m_send_fragment.push_back(sf);
	}

	m_forward_required = true;
	forward(m_proxy->gen_monotonic_ms());
	return  true;
}
}
