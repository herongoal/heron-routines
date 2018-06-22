#include "routine_proxy.h"
#include "log_api.h"
#include "routine.h"


#include <sys/time.h>
#include <cstring>
#include <cstdio>
#include <iostream>


using namespace std;


namespace hrts{
using	namespace std;
int     log_api::gen_prefix(log_level level, char *msg_buf, int buf_len)
{
        static const char *str[] = {"OFF", "FTL", "ERR", "WRN", "INF", "DBG", "TRC", "ALL"};

        gettimeofday(&m_log_timeval, NULL);

        if(m_log_timeval.tv_sec != m_generate_utc)
        {
            localtime_r(&m_generate_utc, &m_generated);
            m_generate_utc = m_log_timeval.tv_sec;
        }

        return  snprintf(msg_buf, buf_len, "%02d-%02d-%02d %02d:%02d:%02d.%06ld-PX%02u-%s:",
                        m_generated.tm_year % 100,
                        m_generated.tm_mon + 1,
                        m_generated.tm_mday,
                        m_generated.tm_hour,
                        m_generated.tm_min,
                        m_generated.tm_sec,
			m_log_timeval.tv_usec,
			m_routine_proxy->get_proxy_id(),
                        str[level]);
}

int     log_api::gen_prefix(log_level level, const routine *rt, char *msg_buf, int buf_len)
{
	static const char *str[] = {"OFF", "FTL", "ERR", "WRN", "INF", "DBG", "TRC", "ALL"};

	unsigned s_addr = rt->m_peer_addr.sin_addr.s_addr;
	uint16_t port = rt->get_port();
	port = port % 256 * 256 + port / 256;

        gettimeofday(&m_log_timeval, NULL);

        if(m_log_timeval.tv_sec != m_generate_utc)
        {
            localtime_r(&m_generate_utc, &m_generated);
            m_generate_utc = m_log_timeval.tv_sec;
        }

        return  snprintf(msg_buf, buf_len, "%02d-%02d-%02d"
                        " %02d:%02d:%02d.%06ld-PX%02u"
                        "-<ip=%d.%d.%d.%d,port=%u,cat=%u,user_flag=%lu,id=%lu,fd=%d>-%s:",
                        m_generated.tm_year % 100,
                        m_generated.tm_mon + 1,
                        m_generated.tm_mday,
                        m_generated.tm_hour,
                        m_generated.tm_min,
                        m_generated.tm_sec,
                        m_log_timeval.tv_usec, m_routine_proxy->get_proxy_id(),
			(int)(s_addr & 0xFF),
			(int)((s_addr >> 8) & 0xFF),
			(int)((s_addr >> 16) & 0xFF),
			(int)(s_addr >> 24),
			port,
                        (int)rt->get_category(),
                        rt->get_user_flag(),
			rt->get_routine_id(), rt->m_fd, str[level]);
}

int     log_api::gen_prefix(uint16_t proxy_id, time_t &gen_utc_sec, struct tm &generated,
	log_level level, const char *file, const int line,
        char *msg_buf, int buf_len)
{
        static const char *str[] = {"OFF", "FTL", "ERR", "WRN", "INF", "DBG", "TRC", "ALL"};


	struct	timeval tv;
	gettimeofday(&tv, NULL);

        if(tv.tv_sec != gen_utc_sec)
        {
		gen_utc_sec = tv.tv_sec;
		localtime_r(&gen_utc_sec, &generated);
        }

        return  snprintf(msg_buf, buf_len, "%02d-%02d-%02d %02d:%02d:%02d.%06ld-PX%02u-<%s:%d>-%s:",
                        generated.tm_year % 100,
                        generated.tm_mon + 1,
                        generated.tm_mday,
                        generated.tm_hour,
                        generated.tm_min,
                        generated.tm_sec,
                        tv.tv_usec,
			proxy_id,
                        file, line, str[level]);
}

void	log_api::log(const char *file_name, int file_line, log_level level, unsigned msg_len, const char *msg)
{
        if(level <= m_level_limit)
        {
                int len = gen_prefix(m_routine_proxy->get_proxy_id(), m_generate_utc, m_generated, level, file_name, file_line, m_msg_buf + m_msg_len, sizeof(m_msg_buf) - m_msg_len);

		m_msg_len += len;

		if(msg_len <= sizeof(m_msg_buf) - m_msg_len)
		{
			memcpy(m_msg_buf + m_msg_len, msg, msg_len);
			m_msg_len += msg_len;
		}
		else
		{
			memcpy(m_msg_buf + m_msg_len, msg, sizeof(m_msg_buf) - m_msg_len);
		}

		if(m_msg_len >= (int)sizeof(m_msg_buf))
		m_msg_len = sizeof(m_msg_buf) - 1;

        //        if(m_msg_len >= m_cache_max)
		{
			m_routine_proxy->send_log(m_msg_buf, m_msg_len);
			m_msg_len = 0;
		}
        }
}

void    log_api::log(log_level level, const char *format, ...)
{
        if(level <= m_level_limit)
        {
                m_msg_len += gen_prefix(level, m_msg_buf + m_msg_len, sizeof(m_msg_buf) - m_msg_len);
		
                va_list ap;
                va_start(ap, format);
                m_msg_len += vsnprintf(m_msg_buf + m_msg_len, sizeof(m_msg_buf) - m_msg_len, format, ap);  
                va_end(ap);

                if(m_msg_len >= (int)sizeof(m_msg_buf))
	        m_msg_len = sizeof(m_msg_buf) - 1;
                
                m_msg_buf[m_msg_len++] = '\n';

                //if(m_msg_len >= m_cache_max)
		{
			m_routine_proxy->send_log(m_msg_buf, m_msg_len);
			m_msg_len = 0;
		}
        }
}

void    log_api::log(routine* rt, log_level level, const char *format, ...)
{
        if(level <= m_level_limit)
        {
                m_msg_len = gen_prefix(level, rt, m_msg_buf + m_msg_len, sizeof(m_msg_buf) - m_msg_len);

                va_list ap;
                va_start(ap, format);
                m_msg_len += vsnprintf(m_msg_buf + m_msg_len, sizeof(m_msg_buf) - m_msg_len, format, ap);  
                va_end(ap);

		const unsigned	omit_limit = sizeof(m_msg_buf) - 50;
                if(m_msg_len > omit_limit)
                {
			int	omit_length = m_msg_len - omit_limit;
                	m_msg_len = omit_limit + snprintf(m_msg_buf + omit_limit,
					sizeof(m_msg_buf) - omit_limit,
					"...(%u extra bytes were omitted)", omit_length);
                }
                
                m_msg_buf[m_msg_len++] = '\n';

                //if(m_msg_len >= sizeof(m_msg_buf) / 2)
                {
                        m_routine_proxy->send_log(m_msg_buf, m_msg_len);
                        m_msg_len = 0;
                }
        }
}
}
