#ifndef _HRTS_HYBRID_LIST_H_
#define _HRTS_HYBRID_LIST_H_


#include <tr1/unordered_map>
#include <map>
#include <stdint.h>
#include <cstdlib>


using   std::tr1::unordered_map;
using   std::map;


namespace   hrts{
struct  list_node_t{
        list_node_t(uint64_t elem_id, void *data, list_node_t *prev, list_node_t *next):
                m_id(elem_id), m_data(data), m_prev(prev), m_next(next)
        {
            //nothing to do here.
        }
        list_node_t(uint64_t elem_id, void *data):
                m_id(elem_id), m_data(data), m_prev(NULL), m_next(NULL)
        {
            //nothing to do here.
        }

        list_node_t():m_id(0u), m_data(NULL), m_prev(NULL), m_next(NULL)
        {
            //nothing to do here.
        }

        const   uint64_t        m_id;
        void*                   m_data;
        struct  list_node_t*    m_prev;
        struct  list_node_t*    m_next;
};

class   hybrid_list{
public:
    hybrid_list():m_list(0u, NULL, NULL, NULL)
    {
            m_list.m_prev = &m_list;
            m_list.m_next = &m_list;
    }

    bool    insert_elem(uint64_t elem_id, void *data);
    void*   search_elem(uint64_t elem_id);
    void*   erase_elem(uint64_t elem_id);
    bool    forward_elem();

    uint32_t elems() const
    {
	    return	m_data.size();
    }

    void*   access_elem()
    {
	    if(m_list.m_next != &m_list)
	    {
		    return	m_list.m_next->m_data;
	    }
	    return	NULL;
    }

private:
    typedef unordered_map<uint64_t, list_node_t*>   hash_map_t;

    hash_map_t          m_data;
    struct  list_node_t m_list;
};//end of class hybrid_list
}//end of namespace hrts


#endif //_HRTS_HYBRID_LIST_H_
