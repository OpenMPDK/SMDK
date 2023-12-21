#ifndef META_API_H_
#define META_API_H_

#include "core/include/internal/init.h"

extern void init_node_stats();
extern void init_system_mem_size(void);
extern size_t get_mem_stats_total(mem_type_t type);
extern size_t get_mem_stats_used(mem_type_t type);
extern size_t get_mem_stats_available(mem_type_t type);
extern size_t get_mem_stats_node_total(mem_type_t type, int node);
extern size_t get_mem_stats_node_available(mem_type_t type, int node);
extern void mem_stats_print(char unit);
extern void stats_per_node_print(char unit);
#endif /* META_API_H_ */
