SUBDIRS=comp_api_c comp_api_cpp metadata_api opt_api_c opt_api_cpp opt_api_nodectl

.PHONY:subdirs $(SUBDIRS)

target:
	for DIRS in $(SUBDIRS);do\
		$(MAKE) -C $$DIRS;\
	done

clean:
	for DIRS in $(SUBDIRS);do\
		$(MAKE) -C $$DIRS clean;\
	done
