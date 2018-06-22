#include "circular_buffer.h"

#include <iostream>
#include <stdint.h>

using namespace std;

struct	default_node{
	uint32_t    	m_capacity;
	uint32_t    	m_start;
	uint32_t    	m_used;
	char        	m_data[4];
};
#pragma pack(1)
struct	packn_node{
	uint32_t    	m_capacity;
	uint32_t    	m_start;
	uint32_t    	m_used;
	char        	m_data[4];
};
#pragma pack()

struct alligned_node{
	uint32_t    	m_capacity;
	uint32_t    	m_start;
	uint32_t    	m_used;
	char        	m_data[4];
}__attribute__((aligned(1)));

struct packed_node{
	uint32_t    	m_capacity;
	uint32_t    	m_start;
	uint32_t    	m_used;
	char        	m_data[4];
}__attribute__((packed));

int	main()
{
	cout << "default size=" << sizeof(default_node) << endl;
	cout << "alligned size=" << sizeof(alligned_node) << endl;
	cout << "packn size=" << sizeof(packn_node) << endl;
	cout << "packed size=" << sizeof(packed_node) << endl;

	const char *str = "0123456789";

	for(int i = 0; i < 10; ++i)
	{
		for(int j = 0; j < 10; ++j)
		{
			circular_buffer *cb = circular_buffer::create(3);
			int append_ret = cb->append(str, i);
			int decr_ret = cb->decr(i);
			if(append_ret != decr_ret)
			{
				cout << "++++++++++++++++shift=" << i << "&len=" << j << endl;
				cout << "unexpected not match." << endl;
				return 0;
			}

			append_ret = cb->append(str, j);
			decr_ret = cb->decr(j);

			if(append_ret != decr_ret)
			{
				cout << "++++++++++++++++shift=" << i << "&len=" << j << endl;
				cout << "unexpected not match." << endl;
				return 0;
			}
			delete	cb;
		}
	}
	cout << "append and decr test passed" << endl;
	for(int i = 0; i < 10; ++i)
        {
                for(int j = 0; j < 10; ++j)
                {
                        circular_buffer *cb = circular_buffer::create(3);
                        int append_ret = cb->append(str, i);
                        int fetch_ret = cb->decr(i);

                        append_ret = cb->append(str, j);
			cout << "++++++++++++++++shift=" << i << "&len=" << j << endl;
			cout << "continuous?=" << (int)cb->data_continuous() << endl;
			char	sss[10];
			fetch_ret = cb->fetch(sss, j);
                        if(string(sss,j) != string(str,j) && j <= 7)
                        {
                                cout << "unexpected fetch not match." << endl;
                                return 0;
                        }
                        delete  cb;
                }
        }
	cout << "append and fetch test passed" << endl;
}
