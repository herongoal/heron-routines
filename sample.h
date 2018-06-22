#include "service.h"
#include <stdint.h>


#include <map>


using   namespace hrts;
using   namespace std;


class   sample_service:public service{
        public:
                sample_service(const string &log_file, log_level level, int8_t proxy_count);
                virtual ~sample_service();

                void    init();

                virtual void    on_routine_read_hup(routine_proxy* proxy, routine* r);
                virtual void    on_routine_peer_hup(routine_proxy* proxy, routine* r);
                virtual void    on_routine_created(uint64_t routine_type, uint64_t routine_id);
                virtual void    on_routine_closed(routine *rt);
                virtual void    on_routine_error(routine_proxy* proxy, routine* r);


                virtual int64_t register_timer(int64_t fight_id, int interval, int times);
                virtual void    delete_timer(int64_t unique_timer_id);
                virtual void    on_timer(routine_proxy *proxy, const timer_info &ti);

                int             process(log_api *api, routine* r, const service_msg_header &header,
                                const char *msg, unsigned msg_len)
		{
			service_msg_header h;
			h.msg_size = 5 + sizeof(h);

			string	s((const char *)&h, sizeof(h));
			s.append("qogir", 5);

			cout << "++++++++++++++++++++++++++++++++++++++++++++++++++++++received:" << msg << endl;
			cout << "++++++++++++++++++++++++++++++++++++++++++++++++++++++response:" << s.size() << endl;

			send_message(1, r->get_routine_id(), s.c_str(), s.size());

			return	0;
		}

		static  void    send_to_center(uint32_t msg_type, const void *buf, unsigned len)
		{
			service::get_instance()->send_via_channel(1, msg_type, buf, len);
		}

		static  void    send_to_match(uint32_t msg_type, const void *buf, unsigned len)
		{
			service::get_instance()->send_via_channel(45, msg_type, buf, len);
		}

		void    send_msg(int64_t user_id, const void *buf, unsigned len);

		static  void    close_routine(int64_t routine_id)
		{
			m_fight_service->clear_routine_info(routine_id);
			m_fight_service->close_routine(routine_id);
		}

		static void     clear_routine_user(int64_t routine_id)
		{
			return  m_fight_service->clear_routine_info(routine_id);
		}

		void    clear_routine_info(int64_t routine_id);

		static int      service_id() {return m_fight_service->m_service_id;}

	public:
		string  m_listen_ipaddr;
		string  m_match_ipaddr;
		string  m_center_ipaddr;

		int     m_listen_port;
		int     m_match_port;
		int     m_center_port;
		int     m_service_id;
	private:
		pthread_rwlock_t                        m_rwlock;
		map<int64_t, int64_t>           m_timer_info;

	private:
		static  sample_service*       m_fight_service;
};
