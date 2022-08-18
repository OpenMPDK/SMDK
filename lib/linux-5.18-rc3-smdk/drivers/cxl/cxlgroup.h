#ifndef __CXL_GROUP_H__
#define __CXL_GROUP_H__
#include "cxlmem.h"

int exmem_init(void);
void exmem_exit(void);
int register_cxl_dvsec_ranges(struct cxl_dev_state *cxlds);
void cxl_map_memdev_to_cxlmemblk(struct cxl_memdev *cxlmd, 
		struct cxl_endpoint_dvsec_info *info);
int add_cxl_info_cfmws(struct device *match, void *data);

#endif /* __CXL_GROUP_H__ */
