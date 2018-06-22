#ifndef	_TIMER_H_
#define	_TIMER_H_


#include <stdint.h>
#include <map>


using	std::map;


namespace hrts{
class	timer_info{
public:
	timer_info(uint64_t timer_id, int64_t interval, int64_t times):
		register_time(0),
		unique_timer_id(timer_id),
		m_times(times),
		m_interval(interval),
		m_execs(0),
		m_total_shift(0),
		m_max_shift(0)
	{
		m_next = m_prev = this;
	}

	timer_info(const timer_info &ti)
	{
		this->register_time = ti.register_time;
		this->unique_timer_id = ti.unique_timer_id;
		this->m_times = ti.m_times;
		this->m_interval = ti.m_interval;
		this->m_execs = ti.m_execs;
		this->m_total_shift = ti.m_total_shift;
		this->m_max_shift = ti.m_max_shift;
		this->m_next = this->m_prev = this;
	}
	timer_info& operator=(const timer_info &ti)
	{
		this->register_time = ti.register_time;
		this->unique_timer_id = ti.unique_timer_id;
		this->m_times = ti.m_times;
		this->m_interval = ti.m_interval;
		this->m_execs = ti.m_execs;
		this->m_total_shift = ti.m_total_shift;
		this->m_max_shift = ti.m_max_shift;
		this->m_next = this->m_prev = this;
		return	*this;
	}

	uint64_t	get_unique_timer_id() const
	{
		return	unique_timer_id;
	}

	int64_t		next_exec_time() const
	{
		return	register_time + (1 + m_execs) * m_interval;
	}

private:
	friend	class		routine_proxy;
	friend	class		timer_manager;

	
	timer_info(): register_time(0),
		unique_timer_id(0),
		m_times(0),
		m_interval(0),
		m_execs(0),
		m_total_shift(0),
		m_max_shift(0)
	{
		m_next = m_prev = this;
	}

	int64_t			register_time;
	uint64_t		unique_timer_id;
	int64_t			m_times;
	int64_t			m_interval;
	int64_t			m_execs;

	int64_t			m_total_shift;
	int64_t			m_max_shift;

	timer_info*		m_prev;
	timer_info*		m_next;
};//end of class timer_info


struct	elapse_time{
	elapse_time():
		m_total(0), m_times(0),
		m_max(-1)
	{
		for(size_t n = 0; n < sizeof(m_elapse_stat) / sizeof(m_elapse_stat[0]); ++n)
		{
			m_elapse_stat[n] = 0;
		}
	}

	void	stat(uint8_t v)
	{
		++m_times;
		m_total += v;
		m_elapse_stat[(size_t)v] += 1;
		if(v > m_max) m_max = v;
	}

	uint8_t	max() const
	{
		return	m_max;
	}

	uint8_t	avg() const
	{
		if(0 == m_times)
		{
			return	0;
		}
		else
		{
			return	m_total / m_times;
		}
	}

	int64_t		m_total;
	int64_t		m_times;
	uint8_t		m_max;

	int64_t		m_elapse_stat[256];
};//end of class elapse_time


class	timer_manager{
public:
	timer_manager(uint8_t timer_base, uint8_t bucket_num, int64_t monotonic_ms):
			m_timer_base(timer_base),
			m_bucket_num(bucket_num),
			m_bucket_idx(monotonic_ms / m_timer_base % m_bucket_num)
	{
			m_buckets = new	timer_info[m_bucket_num];
	}

	~timer_manager()
	{
			for(map<uint64_t, timer_info*>::iterator iter = m_timers.begin();
					iter != m_timers.end(); ++iter)
			{
					timer_info *ti = iter->second;
					delete	ti;
			}
			m_timers.clear();

			delete	[]m_buckets;
			m_buckets = NULL;
	}


	void	forward_timer(log_api* logger, int64_t monotonic_ms)
	{
		timer_info &header = m_buckets[m_bucket_idx];
		if(&header != header.m_next)
		{
			timer_info *ti = header.m_next;
			ti->m_prev->m_next = ti->m_next;
			ti->m_next->m_prev = ti->m_prev;

			if(ti->m_times >= 0 && ti->m_execs >= ti->m_times)
			{
					logger->log(log_trace, "delete_timer timer.unique_timer_id=%ld,"
							"timer.register_times=%ld,timer.exec_interval=%ld,"
							"timer.exec_times=%ld,timer.total_shift=%d,timer.max_shift=%d",
							ti->unique_timer_id,
							ti->register_time, ti->m_interval,
							ti->m_execs, ti->m_total_shift, ti->m_max_shift);

					m_timers.erase(ti->unique_timer_id);
					delete	ti;
					ti = NULL;
					if(&header != header.m_next)
					{
						return	;
					}
			}

			int64_t	bucket_id = ti->next_exec_time() / m_timer_base % m_bucket_num;
			{
					timer_info &header = m_buckets[bucket_id];

					ti->m_prev = header.m_prev;
					ti->m_next = &header;
					header.m_prev = ti;
					ti->m_prev->m_next = ti;
			}

			while(&m_buckets[m_bucket_idx] == m_buckets[m_bucket_idx].m_next)
			{
				m_bucket_idx = (m_bucket_idx + 1) % m_bucket_num;
			}
		}
	}

	timer_info*	get_latest_timer(int64_t monotonic_ms)
	{
		timer_info &header = m_buckets[m_bucket_idx];
		if(&header == header.m_next)
		{
			return	NULL;
		}

		return	header.m_next;
	}

	bool	register_timer(log_api *logger, int64_t monotonic_ms, const timer_info &ti)
	{
		if(m_timers.count(ti.unique_timer_id) == 0)
		{
			timer_info *node = new timer_info(ti);
			node->register_time = monotonic_ms;
			int64_t	shift = node->next_exec_time() / m_timer_base % m_bucket_num;
			timer_info &header = m_buckets[shift];

			node->m_prev = header.m_prev;
			node->m_next = &header;
			header.m_prev = node;
			node->m_prev->m_next = node;

			m_timers[node->unique_timer_id] = node;

			logger->log(log_trace, "register_timer.unique_timer_id=%lu,shift=%ld finished"
					",next_exec_time=%ld",
					node->unique_timer_id, shift, node->next_exec_time());
			while(&m_buckets[m_bucket_idx] == m_buckets[m_bucket_idx].m_next)
			{
				m_bucket_idx = (m_bucket_idx + 1) % m_bucket_num;
			}
			return        true;
		}
		else
		{
			logger->log(log_error, "register_timer.unique_timer_id=%lu denied",
					ti.unique_timer_id);
			return        false;
		}
	}

	void	delete_timer(log_api *logger, uint64_t unique_timer_id)
	{
		map<uint64_t, timer_info*>::iterator pos = m_timers.find(unique_timer_id);
		if(pos != m_timers.end())
		{
			timer_info *ti = pos->second;

			ti->m_prev->m_next = ti->m_next;
			ti->m_next->m_prev = ti->m_prev;

			ti->m_prev = NULL;
			ti->m_next = NULL;

			m_timers.erase(pos);

			logger->log(log_trace, "delete_timer.unique_timer_id=%lu,"
					"timer.register_time=%ld,timer.exec_interval=%ld,"
					"timer.exec_times=%ld,timer.total_shift=%d,timer.max_shift=%d",
					ti->unique_timer_id,
					ti->register_time, ti->m_interval,
					ti->m_execs, ti->m_total_shift, ti->m_max_shift);

			delete        ti;
		}
		else
		{
			logger->log(log_error, "delete_timer.unique_timer_id=%lu not exist",
					unique_timer_id);
		}
	}

private:
	timer_manager&	operator=(const timer_manager& tm);
	timer_manager(const timer_manager &tm);

	const	uint16_t		m_timer_base;
	const	uint16_t		m_bucket_num;
	uint16_t			m_bucket_idx;
	map<uint64_t, timer_info*>	m_timers;
	timer_info*			m_buckets;
};
}//end of namespace hrts


#endif //_TIMER_H_
