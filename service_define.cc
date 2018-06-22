#include "service_define.h"


#include <cstdio>
#include <iostream>


using namespace std;


namespace	hrts{
bool    g_big_edian;

bool	test_byte_order()
{
		union{
			char	chr[2];
			short	val;
		}u;

		u.val = 0x0100;
		return (bool)u.chr[0];
}

void    report_event(const char *format, ...)
{
	char	msg[4096];
	va_list ap;
	va_start(ap, format);
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);
	cout << msg << endl;
}

uint64_t    ipc_msg_header::next_unique_msg_id = 1;

}//end of namespace hrts
