#ifndef SMALLOC_H_
#define SMALLOC_H_

extern int init_smalloc(void);
extern void terminate_smalloc(void);
extern void intersect_bitmask(mem_type_t type, struct bitmask *nodemask);

#endif /* SMALLOC_H_ */
