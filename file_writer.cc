#include "file_writer.h"
#include "routine.h"


#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>


#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <iostream>


namespace hrts{
using	namespace std;

file_writer::file_writer(const string &file_name, size_t slice_kb):
	m_file_fd(-1), m_file_name(file_name),
	m_slice_kb(slice_kb)
{
	static	const	mode_t	file_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	static	const	mode_t	file_flag = O_WRONLY | O_APPEND | O_CREAT;
	m_file_fd = open(m_file_name.c_str(), file_flag, file_mode);
	fcntl(m_file_fd, F_SETFL, O_NONBLOCK);
	lseek(m_file_fd, 0, SEEK_END);
}

file_writer::~file_writer()
{
	close(m_file_fd);
}

void    file_writer::append(const char *msg, unsigned msg_len)
{
	m_write_buffer.append(msg, msg_len);
	ssize_t ret = write(m_file_fd, m_write_buffer.c_str(), m_write_buffer.size());
	if(ret > 0)
	{
		m_write_buffer = m_write_buffer.substr(ret);
	}
	else
	{
		cout << "write failed:" << strerror(errno) << endl;
	}
}
}
