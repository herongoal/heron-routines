#include "hybrid_list.h"


namespace   hrts{
bool    hybrid_list::insert_elem(uint64_t elem_id, void *data)
{
    hash_map_t::iterator pos = m_data.find(elem_id);
    if(pos != m_data.end())
    {
        return  false;
    }

    list_node_t *elem = new list_node_t(elem_id, data);

    m_data[elem->m_id] = elem;
    elem->m_prev = m_list.m_prev;
    elem->m_next = &m_list;
    m_list.m_prev = elem;
    elem->m_prev->m_next = elem;

    return  true;
}

void*   hybrid_list::search_elem(uint64_t elem_id)
{
    hash_map_t::iterator pos = m_data.find(elem_id);
    if(pos != m_data.end())
    {
        list_node_t *node = pos->second;
        return  node->m_data;
    }

    return  NULL;
}

bool    hybrid_list::forward_elem()
{
    if(m_list.m_next != &m_list)
    {
        m_list.m_prev->m_next = m_list.m_next;
        m_list.m_next->m_prev = m_list.m_prev;

        m_list.m_prev = m_list.m_next;
        m_list.m_next = m_list.m_next->m_next;

        m_list.m_next->m_prev = &m_list;
        m_list.m_prev->m_next = &m_list;
        return  true;
    }

    return  false;
}

void*   hybrid_list::erase_elem(uint64_t elem_id)
{
    hash_map_t::iterator pos = m_data.find(elem_id);
    if(pos == m_data.end())
    {
        return  NULL;
    }

    list_node_t *node = pos->second;
    void*   data = node->m_data;
    m_data.erase(pos);

    node->m_prev->m_next = node->m_next;
    node->m_next->m_prev = node->m_prev;
    delete  node;

    return  data;
}

}//end of namespace hrts
