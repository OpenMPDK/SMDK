SUBDIRS=group_policy heap_allocator lazy_alloc mmap numactl subzone syscall

.PHONY:subdirs $SUBDIRS)

target:
	for DIRS in $(SUBDIRS);do\
		$(MAKE) -C $$DIRS;\
	done

clean:
	for DIRS in $(SUBDIRS);do\
		$(MAKE) -C $$DIRS clean;\
	done
