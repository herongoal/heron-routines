#ifndef _HRTS_FILE_WRITER_H_
#define _HRTS_FILE_WRITER_H_


#include <stdint.h>
#include <string>


namespace   hrts{
using	std::string;


class   file_writer{
public:
        file_writer(const std::string &file, size_t slice_size_limit);
        ~file_writer();

	void	append(const char *data, unsigned len);
private:
	uint64_t	m_write_bytes;
	uint64_t	m_slice_bytes;
	int		m_slice_id;
	int		m_file_fd;
	string		m_write_buffer;

private:
	const string	m_file_name;
	const size_t	m_slice_kb;
};//end of class file_writer
}//end of namespace hrts 


#endif  //_HRTS_FILE_WRITER_H_
