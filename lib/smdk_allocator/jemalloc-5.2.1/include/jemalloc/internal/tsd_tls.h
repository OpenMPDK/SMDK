#ifdef JEMALLOC_INTERNAL_TSD_TLS_H
#error This file should be included only once, by tsd.h.
#endif
#define JEMALLOC_INTERNAL_TSD_TLS_H

#define JEMALLOC_TSD_TYPE_ATTR(type) __thread type JEMALLOC_TLS_MODEL

typedef struct {
    tsd_t val[2];
    int cur_type;
} tsd_set_t;
extern JEMALLOC_TSD_TYPE_ATTR(tsd_set_t) tsd_set_tls;
extern pthread_key_t tsd_tsd;
extern bool tsd_booted;

/* Initialization/cleanup. */
JEMALLOC_ALWAYS_INLINE bool
tsd_boot0(void) {
	if (pthread_key_create(&tsd_tsd, &tsd_cleanup) != 0) {
		return true;
	}
	tsd_booted = true;
	return false;
}

JEMALLOC_ALWAYS_INLINE void
tsd_boot1(void) {
    tsd_set_tls.val[0].memtype = MEM_ZONE_NORMAL; /* mem_zone_normal */
    tsd_set_tls.val[1].memtype = MEM_ZONE_EXMEM; /* mem_zone_exmem */
}

JEMALLOC_ALWAYS_INLINE bool
tsd_boot(void) {
    if (tsd_boot0()) {
        return true;
    }
    tsd_boot1();
    return false;
}

JEMALLOC_ALWAYS_INLINE bool
tsd_booted_get(void) {
	return tsd_booted;
}

JEMALLOC_ALWAYS_INLINE bool
tsd_get_allocates(void) {
	return false;
}

/* Get/set. */
JEMALLOC_ALWAYS_INLINE void
set_tsd_set_cur_mem_type(int type) {
    tsd_set_tls.cur_type = type;
}

JEMALLOC_ALWAYS_INLINE void
change_tsd_set_cur_mem_type(void) {
    tsd_set_tls.cur_type = !(tsd_set_tls.cur_type);
}

JEMALLOC_ALWAYS_INLINE tsd_t *
tsd_get_with_type(int type){
    return &(tsd_set_tls.val[type]);
}

JEMALLOC_ALWAYS_INLINE tsd_t *
tsd_get(bool init) {
    return &(tsd_set_tls.val[tsd_set_tls.cur_type]);
}

JEMALLOC_ALWAYS_INLINE void
tsd_set(tsd_t *val) {
	assert(tsd_booted);
    if (likely(&(tsd_set_tls.val[tsd_set_tls.cur_type]) != val)) {
        tsd_set_tls.val[tsd_set_tls.cur_type] = (*val);
    }
    if (pthread_setspecific(tsd_tsd, (void *)(&tsd_set_tls)) != 0) {
		malloc_write("<jemalloc>: Error setting tsd.\n");
		if (opt_abort) {
			abort();
		}
	}
}
