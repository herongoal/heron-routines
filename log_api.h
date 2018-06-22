#ifndef _HRTS_LOG_API_H_
#define _HRTS_LOG_API_H_


#include <stdarg.h>
#include <time.h>
#include <cstring>
#include <sys/time.h>
#include <stdint.h>


namespace   hrts{
enum    log_level{
	log_off = 0,
	log_fatal = 1,
	log_error = 2,
	log_warn = 3,
	log_info = 4,
	log_debug = 5,
	log_trace = 6,
	log_all = 7,
};


class	routine_proxy;
class	routine;


class   log_api{
public:
	log_api(routine_proxy *rp, log_level level): m_routine_proxy(rp),
		m_level_limit(level),
		m_cache_max(32 * 1024),
		m_msg_len(0), m_generate_utc(-1)
	{
	}

	~log_api()
	{
		//nothing to do.
	}

	void	log(const char *file_name, int file_line, log_level level, unsigned len, const char *msg);
	void	log(log_level level, const char *format, ...);
	void	log(routine *r, log_level level, const char *format, ...);

private:
	static	int	gen_prefix(uint16_t proxy_id, time_t &gen_utc_sec, struct tm &generated,
			log_level level, const char *file, const int line,
			char *msg_buf, int buf_len);

	int     gen_prefix(log_level level, const routine *r, char *msg_buf, int buf_len);
	int     gen_prefix(log_level level, char *msg_buf, int buf_len);

private:
	routine_proxy*  m_routine_proxy;
	const log_level m_level_limit;
	const int	m_cache_max;

	char		m_msg_buf[65536];
	int		m_msg_len;
	struct	timeval	m_log_timeval;
	time_t		m_generate_utc;
	struct	tm	m_generated;
};
}


#endif  //_HRTS_LOG_API_H_
