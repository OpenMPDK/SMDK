SUBDIRS=driver heap_allocator mmap numactl subzone syscall cxl_cli cxlswap

.PHONY:subdirs $SUBDIRS)

target:
	for DIRS in $(SUBDIRS);do\
		$(MAKE) -C $$DIRS;\
	done

clean:
	for DIRS in $(SUBDIRS);do\
		$(MAKE) -C $$DIRS clean;\
	done
