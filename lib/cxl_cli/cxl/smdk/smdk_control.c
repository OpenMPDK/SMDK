#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <util/util.h>
#include <cxl/libcxl.h>
#include <cxl/builtin.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <cpuid.h>
#include <signal.h>
#include <numa.h>

#define IOMEM "/proc/iomem"
#define SYSFS_KMEM_NEWID   "/sys/bus/dax/drivers/kmem/new_id"
#define SYSFS_KMEM_BIND	   "/sys/bus/dax/drivers/kmem/bind"

#define SYSFS_CXL_DEVICES "/sys/kernel/cxl/devices"
#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"

#define SYSFS_NODE "/sys/devices/system/node/"
#define SYSFS_NODE_POSSIBLE "/sys/devices/system/node/possible"
#define SYSFS_NODE_HAS_CPU "/sys/devices/system/node/has_cpu"

#define SYSFS_CXLSWAP_ENABLED "/sys/module/cxlswap/parameters/enabled"
#define SYSFS_CXLSWAP_FLUSH "/sys/module/cxlswap/parameters/flush"

#define SYSFS_CXLCACHE_ENABLED "/sys/module/cxlcache/parameters/enabled"
#define SYSFS_CXLCACHE_FLUSH "/sys/module/cxlcache/parameters/flush"

#define MAX_NUMA_NODES (64)
#define MAX_CHAR_LEN (1024)
#define INFO_LEN       (16)
#define MAX_ULLONG_LEN (32)

#define PRINT_EVERY_NODE (-2)

#define HWP_ENABLE (0x0)
#define HWP_DISABLE (0xF)
#define PATH_MSR "/dev/cpu"

#define NODE_CPU (1 << 0)
#define NODE_MEM (1 << 2)

#define OFFSET_MODEL (4)
#define OFFSET_EXT_MODEL (16)

struct memdev_pci_info {
	int id;
	char pci_bus_addr[INFO_LEN];
	char pci_cur_link_speed[INFO_LEN];
	char pci_cur_link_width[INFO_LEN];
};

struct cxl_dev_info {
	int num;
	int node_id;
	int socket_id;
	size_t start;
	size_t size;
	int state;
	int num_memdev;
	struct memdev_pci_info *memdev;
	char name_daxdev[INFO_LEN];
};

int nr_socket;
int nr_cxl_devs;
int max_node;
struct cxl_dev_info *cxl_info;

int get_model_id(void)
{
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	unsigned int model, ext_model;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0) {
		/* cpuid not supported */
		return 0;
	}
	/* 
		7:4 -  Model
		19:16 - Extended Model ID
		Model ID = Extended Model ID << 4 | Model
	*/
	model = (eax >> OFFSET_MODEL) & 0xf;
	ext_model = (eax >> OFFSET_EXT_MODEL) & 0xf;

	return ext_model << 4 | model;
}

static uint32_t get_msr_offset(void)
{
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	char vendor[13] = {
		0,
	};
	unsigned int model;
	if (__get_cpuid(0, &eax, &ebx, &ecx, &edx) == 0) {
		/* cpuid not supported */
		return 0;
	}
	*(unsigned int *)vendor = ebx;
	*(unsigned int *)(vendor + 4) = edx;
	*(unsigned int *)(vendor + 8) = ecx;

	if (!strncmp(vendor, "GenuineIntel", 12)) {
		model = get_model_id();
		if (model == 0xf || model == 0x1d || model == 0x16 ||
		    model == 0x17) /* core 2 family */
			return 0x1a0;
		else
			return 0x1a4;
	} else if (!strncmp(vendor, "AuthenticAMD", 12)) {
		return 0xc0000108;
	} else {
		return 0;
	}
}

struct range {
	uint64_t start;
	uint64_t end;
};

static inline uint64_t range_len(const struct range *range)
{
	return range->end - range->start + 1;
}

static int get_memdev_dvsec_ranges(int mdid, struct range *dvsec_ranges)
{
	char str[MAX_CHAR_LEN], mdpath[MAX_CHAR_LEN];
	FILE *fp;
	int cnt = 0;

	sprintf(mdpath, "/sys/bus/cxl/devices/mem%d/dvsec_ranges", mdid);
	fp = fopen(mdpath, "r");
	if (!fp)
		return 0;

	while (fgets(str, sizeof(str), fp) != NULL) {
		if (sscanf(str, "%lx-%lx", &dvsec_ranges[cnt].start,
			   &dvsec_ranges[cnt].end) != 2) {
			fclose(fp);
			return cnt;
		}
		if (++cnt == 2)
			break;
	}

	fclose(fp);
	return cnt;
}

static int get_nr_cxl_devs(void)
{
	struct dirent **devs;
	nr_cxl_devs = scandir(SYSFS_CXL_DEVICES, &devs, NULL, alphasort) - 2;
	return (nr_cxl_devs < 0) ? 0 : nr_cxl_devs;
}

static int get_nr_socket(void)
{
	int nr_node_cpu = 0;
	char str[10];
	FILE *fp;
	fp = fopen(SYSFS_NODE_HAS_CPU, "r");
	if (!fp) {
		error("cannot open %s.\nAborting.", SYSFS_NODE_HAS_CPU);
		exit(1);
	}
	if (fgets(str, 10, fp) == NULL) {
		error("cannot get content of %s.\nAborting.",
		      SYSFS_NODE_HAS_CPU);
		fclose(fp);
		exit(1);
	}
	if (!sscanf(str, "0-%d", &nr_node_cpu)) {
		if (!sscanf(str, "%d", &nr_node_cpu) || nr_node_cpu != 0) {
			error("cannot get content of %s.\nAborting.",
			      SYSFS_NODE_HAS_CPU);
			fclose(fp);
			exit(1);
		}
	}
	nr_node_cpu++;
	fclose(fp);
	return nr_node_cpu;
}

static int get_dev_state(int dev)
{
	char path[MAX_CHAR_LEN];
	char str[MAX_CHAR_LEN];
	FILE *fp;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/state", dev);
	if (access(path, R_OK)) {
		error("%s is not accessible.", path);
		return -1;
	}
	fp = fopen(path, "r");
	if (!fp) {
		error("cannot open %s.", path);
		return -1;
	}
	if (fgets(str, 20, fp) == NULL) {
		error("cannot get content of %s.\nAborting.", path);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	if (!strncmp(str, "online", 6))
		return 1;
	else if (!strncmp(str, "offline", 7))
		return 0;
	else
		return -1;
}

static int get_dev_node(int dev)
{
	char path[MAX_CHAR_LEN];
	char str[MAX_CHAR_LEN];
	FILE *fp;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/node_id", dev);
	if (access(path, R_OK)) {
		error("%s is not accessible.\nAborting.", path);
		exit(1);
	}
	fp = fopen(path, "r");
	if (!fp) {
		error("cannot open %s.\nAborting.", path);
		exit(1);
	}
	if (fgets(str, 20, fp) == NULL) {
		error("cannot get content of %s.\nAborting.", path);
		fclose(fp);
		exit(1);
	}
	fclose(fp);
	return atoi(str);
}

static int get_dev_socket(int dev)
{
	char path[MAX_CHAR_LEN];
	char str[MAX_CHAR_LEN];
	FILE *fp;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/socket_id", dev);
	if (access(path, R_OK)) {
		error("%s is not accessible.\nAborting.", path);
		exit(1);
	}
	fp = fopen(path, "r");
	if (!fp) {
		error("cannot open %s.\nAborting.", path);
		exit(1);
	}
	if (fgets(str, 20, fp) == NULL) {
		error("cannot get content of %s.\nAborting.", path);
		exit(1);
	}
	fclose(fp);
	return atoi(str);
}

static size_t get_dev_start_addr(int dev)
{
	char path[MAX_CHAR_LEN];
	char str[MAX_CHAR_LEN];
	size_t start_addr;
	FILE *fp;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/start_address", dev);
	if (access(path, R_OK)) {
		error("%s is not accessible.\nAborting.", path);
		exit(1);
	}
	fp = fopen(path, "r");
	if (!fp) {
		error("cannot open %s.\nAborting.", path);
		exit(1);
	}
	if (fgets(str, 20, fp) == NULL) {
		error("cannot get content of %s.\nAborting.", path);
		fclose(fp);
		exit(1);
	}
	fclose(fp);
	start_addr = (size_t)strtol(str + 2, NULL, 16);
	return start_addr;
}

static size_t get_dev_size(int dev)
{
	char path[MAX_CHAR_LEN];
	char str[MAX_CHAR_LEN];
	size_t size;
	FILE *fp;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/size", dev);
	if (access(path, R_OK)) {
		error("%s is not accessible.\nAborting.", path);
		exit(1);
	}
	fp = fopen(path, "r");
	if (!fp) {
		error("cannot open %s.\nAborting.", path);
		exit(1);
	}
	if (fgets(str, 20, fp) == NULL) {
		error("cannot get content of %s.\nAborting.", path);
		fclose(fp);
		exit(1);
	}
	fclose(fp);
	size = (size_t)strtol(str + 2, NULL, 16);
	return size;
}

static int get_num_memdev(struct cxl_dev_info *dev)
{
	int num_memdev;
	DIR *d;
	struct dirent *de;
	char path[MAX_CHAR_LEN];

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/", dev->num);
	num_memdev = 0;
	d = opendir(path);
	if (d) {
		while ((de = readdir(d)) != NULL) {
			if (strncmp(de->d_name, "mem", 3))
				continue;
			num_memdev++;
		}
		closedir(d);
	}
	return num_memdev;
}

static void get_dev_memdev(struct cxl_dev_info *dev)
{
	DIR *d;
	struct dirent *de;
	char path[MAX_CHAR_LEN];
	int memdev_id = 0;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/", dev->num);
	dev->num_memdev = get_num_memdev(dev);
	if (!dev->num_memdev) {
		errno = EINVAL;
		return;
	}

	dev->memdev = (struct memdev_pci_info *)calloc(
		dev->num_memdev, sizeof(struct memdev_pci_info));
	if (!dev->memdev) {
		errno = ENOMEM;
		return;
	}

	d = opendir(path);
	if (d) {
		char *endptr;
		while ((de = readdir(d)) != NULL) {
			if (strncmp(de->d_name, "mem", 3))
				continue;
			dev->memdev[memdev_id++].id =
				strtol(de->d_name + 3, &endptr, 0);
			if (endptr == (de->d_name + 3)) //no conversion case
				continue;
		}
		closedir(d);
	}
}

static void get_name_daxdev(struct cxl_dev_info *dev)
{
	DIR *d;
	struct dirent *de;
	char path[MAX_CHAR_LEN];

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/", dev->num);
	d = opendir(path);
	if (d) {
		while ((de = readdir(d)) != NULL) {
			if (!strncmp(de->d_name, "dax", 3)) {
				strcpy(dev->name_daxdev, de->d_name);
				break;
			}
		}
		closedir(d);
	}
}

static void get_dev_memdev2(struct cxl_dev_info *dev)
{
	DIR *d;
	struct dirent *de;
	char path[MAX_CHAR_LEN];
	struct range dvsec_ranges[2];
	int mdid, cnt;

	if (!dev)
		return;

	dev->memdev = (struct memdev_pci_info *)calloc(
		2, sizeof(struct memdev_pci_info));
	if (!dev->memdev) {
		errno = ENOMEM;
		return;
	}

	sprintf(path, "/sys/bus/cxl/devices/");
	dev->num_memdev = 0;
	d = opendir(path);
	if (!d)
		return;

	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, "mem", 3))
			continue;
		mdid = strtol(de->d_name + 3, NULL, 0);

		cnt = get_memdev_dvsec_ranges(mdid, dvsec_ranges);
		for (int i = 0; i < cnt; i++) {
			if (dev->start == dvsec_ranges[i].start &&
			    dev->size == range_len(&dvsec_ranges[i])) {
				dev->memdev[dev->num_memdev++].id = mdid;
			}
		}
	}

	closedir(d);
}

static void get_dev_pci_info(struct cxl_dev_info *dev)
{
	char path_symlink[MAX_CHAR_LEN] = {
		0,
	};
	char path_bus_addr[MAX_CHAR_LEN] = {
		0,
	};
	char path_link_speed[MAX_CHAR_LEN] = {
		0,
	};
	char path_link_width[MAX_CHAR_LEN] = {
		0,
	};
	char *tok;
	FILE *fp;
	size_t read_len;
	int sz_link;

	for (int i = 0; i < dev->num_memdev; i++) {
		char *tokens[100];
		int token_cnt = 0;

		/* get pci bus addr */
		sprintf(path_symlink, "/sys/kernel/cxl/devices/cxl%d/mem%d",
			dev->num, dev->memdev[i].id);
		sz_link = readlink(path_symlink, path_bus_addr, MAX_CHAR_LEN);
		if (sz_link < 0) {
			sprintf(path_symlink, "/sys/bus/cxl/devices/mem%d",
				dev->memdev[i].id);
			sz_link = readlink(path_symlink, path_bus_addr,
					   MAX_CHAR_LEN);
			if (sz_link < 0)
				return;
		}
		tok = strtok(path_bus_addr, "/");
		while (tok != NULL) {
			tokens[token_cnt++] = tok;
			tok = strtok(NULL, "/");
		}
		strcpy(dev->memdev[i].pci_bus_addr, tokens[token_cnt - 2]);

		/* get link speed */
		sprintf(path_link_speed, "%s/%s/current_link_speed",
			SYSFS_PCI_DEVICES, dev->memdev[i].pci_bus_addr);
		if (!access(path_link_speed, R_OK)) {
			fp = fopen(path_link_speed, "r");
			if (fp) {
				read_len =
					fread(dev->memdev[i].pci_cur_link_speed,
					      1, INFO_LEN, fp);
				dev->memdev[i].pci_cur_link_speed[read_len - 1] =
					(char)0; //remove newline
				fclose(fp);
			}
		}

		/* get link width */
		sprintf(path_link_width, "%s/%s/current_link_width",
			SYSFS_PCI_DEVICES, dev->memdev[i].pci_bus_addr);
		if (!access(path_link_width, R_OK)) {
			fp = fopen(path_link_width, "r");
			if (fp) {
				read_len =
					fread(dev->memdev[i].pci_cur_link_width,
					      1, INFO_LEN, fp);
				dev->memdev[i].pci_cur_link_width[read_len - 1] =
					(char)0; //remove newline
				if (strcmp(dev->memdev[i].pci_cur_link_width,
					   "0") == 0)
					sprintf(dev->memdev[i]
							.pci_cur_link_width,
						"Unknown");
				fclose(fp);
			}
		}
	}
}

static void init_cxl_dev_info(void)
{
	int i;

	nr_cxl_devs = get_nr_cxl_devs();
	nr_socket = get_nr_socket();
	max_node = numa_max_node();
	cxl_info = calloc(nr_cxl_devs, sizeof(struct cxl_dev_info));
	if (!cxl_info) {
		errno = ENOMEM;
		return;
	}

	for (i = 0; i < nr_cxl_devs; i++) {
		cxl_info[i].num = i;
		cxl_info[i].node_id = get_dev_node(i);
		cxl_info[i].socket_id = get_dev_socket(i);
		cxl_info[i].state = get_dev_state(i);
		cxl_info[i].start = get_dev_start_addr(i);
		cxl_info[i].size = get_dev_size(i);
		get_name_daxdev(&cxl_info[i]);
		get_dev_memdev(&cxl_info[i]);
		if (cxl_info[i].num_memdev == 0)
			get_dev_memdev2(&cxl_info[i]);
		get_dev_pci_info(&cxl_info[i]);
	}
}

static void finalize_cxl_dev_info(void)
{
	int i;
	if (cxl_info) {
		for (i = 0; i < nr_cxl_devs; i++) {
			if (cxl_info[i].memdev)
				free(cxl_info[i].memdev);
		}
		free(cxl_info);
	}
}

static int _write_to_path(const char *path, const char *buf, int quiet)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	int n, len = strlen(buf) + 1;

	if (fd < 0) {
		error("cannot open %s.\n", path);
		return EXIT_FAILURE;
	}

	n = write(fd, buf, len);
	close(fd);
	if (n < len) {
		if (!quiet)
			error("Failed to write %s to %s: %s\n", buf, path,
			      strerror(errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int write_to_path(const char *path, const char *buf)
{
	return _write_to_path(path, buf, 0);
}

static int write_to_path_quiet(const char *path, const char *buf)
{
	return _write_to_path(path, buf, 1);
}

static int dev_change_node(int dev, int target_node)
{
	char path[MAX_CHAR_LEN], buf[MAX_CHAR_LEN];

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/node_id", dev);
	if (access(path, W_OK)) {
		error("%s is not accessible.\n", path);
		return EXIT_FAILURE;
	}
	sprintf(buf, "%d", target_node);
	return write_to_path(path, buf);
}

static int dax_remove_id(int dev)
{
	char *devname = cxl_info[dev].name_daxdev;
	char path[MAX_CHAR_LEN];
	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/%s/driver/remove_id", dev,
		devname);
	if (access(path, W_OK)) {
		error("%s is not accessible.\n", path);
		return EXIT_FAILURE;
	}
	return write_to_path(path, devname);
}

static int dax_unbind(int dev)
{
	char *devname = cxl_info[dev].name_daxdev;
	char path[MAX_CHAR_LEN];
	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/%s/driver/unbind", dev,
		devname);
	if (access(path, W_OK)) {
		error("%s is not accessible.\n", path);
		return EXIT_FAILURE;
	}
	return write_to_path(path, devname);
}

static int kmem_new_id(int dev)
{
	char *devname = cxl_info[dev].name_daxdev;
	return write_to_path(SYSFS_KMEM_NEWID, devname);
}

static int kmem_bind(int dev)
{
	char *devname = cxl_info[dev].name_daxdev;
	return write_to_path_quiet(SYSFS_KMEM_BIND, devname);
}

#define RUN_AND_CHECK_FAIL(n)            \
	do {                             \
		ret = n;                 \
		if (ret == EXIT_FAILURE) \
			goto exit;       \
	} while (0)

static int register_kmem(int dev, int node_id)
{
	int ret = 0;
	char path[MAX_CHAR_LEN];

	RUN_AND_CHECK_FAIL(dax_remove_id(dev));
	RUN_AND_CHECK_FAIL(dax_unbind(dev));
	RUN_AND_CHECK_FAIL(kmem_new_id(dev));
	kmem_bind(dev);
	RUN_AND_CHECK_FAIL(dev_change_node(dev, node_id));

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/%s/driver", dev,
		cxl_info[dev].name_daxdev);
	if (access(path, F_OK))
		ret = EXIT_FAILURE;

exit:
	return ret;
}

#undef RUN_AND_CHECK_FAIL

static int mode_node(int *count)
{
	int ret, i = 0;
	int target_node;
	bool invalid_socket_id = false;

	for (i = 0; i < nr_cxl_devs; i++) {
		if (cxl_info[i].socket_id != -1) {
			target_node = nr_socket + cxl_info[i].socket_id;
		} else {
			target_node = nr_socket;
			invalid_socket_id = true;
		}

		if (target_node == cxl_info[i].node_id)
			continue;

		ret = register_kmem(i, target_node);
		if (ret == EXIT_FAILURE)
			return ret;
	}
	*count = (invalid_socket_id) ? 1 : nr_socket;
	return EXIT_SUCCESS;
}

static int mode_noop(int *count)
{
	int ret, i = 0;
	int target_node = nr_socket;
	for (i = 0; i < nr_cxl_devs; i++) {
		if (target_node != cxl_info[i].node_id) {
			ret = register_kmem(i, target_node);
			if (ret == EXIT_FAILURE)
				return ret;
		}
		target_node++;
	}
	*count = nr_cxl_devs;
	return EXIT_SUCCESS;
}

static void _print_cxl_info_dev(int dev_id)
{
	char id[MAX_CHAR_LEN];
	sprintf(id, "cxl%d", cxl_info[dev_id].num);
	printf("   {\n");
	printf("      \"id\":\"%s\",\n", id);
	printf("      \"start_address\":\"0x%lx\",\n", cxl_info[dev_id].start);
	printf("      \"size\":\"0x%lx\",\n", cxl_info[dev_id].size);
	printf("      \"node_id\":\"%d\",\n", cxl_info[dev_id].node_id);
	printf("      \"socket_id\":\"%d\",\n", cxl_info[dev_id].socket_id);
	if (cxl_info[dev_id].state == 1)
		printf("      \"state\":\"online\",\n");
	else if (cxl_info[dev_id].state == 0)
		printf("      \"state\":\"offline\",\n");
	else
		printf("      \"state\":\"error\",\n");
	if (cxl_info[dev_id].num_memdev > 0)
		printf("      \"memdev\":\n");
	for (int i = 0; i < cxl_info[dev_id].num_memdev; i++) {
		printf("      {\n");
		printf("         \"memdev_id\":\"%d\",\n",
		       cxl_info[dev_id].memdev[i].id);
		printf("         \"pci_bus_addr\":\"%s\",\n",
		       cxl_info[dev_id].memdev[i].pci_bus_addr);
		printf("         \"pci_cur_link_speed\":\"%s\",\n",
		       cxl_info[dev_id].memdev[i].pci_cur_link_speed);
		printf("         \"pci_cur_link_width\":\"%s\",\n",
		       cxl_info[dev_id].memdev[i].pci_cur_link_width);
		printf("      }%s",
		       (i < (cxl_info[dev_id].num_memdev - 1) ? ",\n" : "\n"));
	}
	printf("   }\n");
}

static void print_usage_get_latency_matrix(void)
{
	printf("*** cxl get-latency-matrix cmd usage ***\n\n");
	printf("\tcxl get-latency-matrix [Options...]\n\n");
	printf("\t\t   --size <MB>: size(range) of test buffer in MiBs (default: 20000MiB)\n\n");
	printf("\t\t   --stride <Bytes>: stride length in bytes (default: 64B)\n");
	printf("\t\t                     *stride cannot be larger than the size\n\n");
	printf("\t\t   --random: to measure latencies with random access (default: sequential access)\n\n");
	printf("\t\t   --no-change-prefetcher: not to change hw prefetcher before starting test (default: turn-off hw prefetcher before test)\n\n");
	printf("\t\t   --iteration <n>: iterate n times (default: iterate only 1 time)\n\n");
}

static int print_cxl_info_dev(char *dev)
{
	int i;
	int dev_id;

	printf("[\n");

	if (dev == NULL) {
		for (i = 0; i < nr_cxl_devs; i++)
			_print_cxl_info_dev(i);
	} else {
		if ((!sscanf(dev, "cxl%d", &dev_id)) ||
		    (dev_id < 0 || dev_id >= nr_cxl_devs))
			goto inval_option;

		_print_cxl_info_dev(dev_id);
	}

	printf("]\n");
	return EXIT_SUCCESS;

inval_option:
	error("set valid cxl device(s) \n");
	return -EINVAL;
}

static int print_cxl_info_node(int target_node)
{
	int node = -1;
	int i, j = 0;
	int devs = 0;
	if ((target_node != PRINT_EVERY_NODE) &&
	    (target_node < -1 || target_node > max_node))
		goto inval_option;
	printf("[\n");
	for (i = -1; i <= max_node; i++) {
		if (target_node != PRINT_EVERY_NODE) {
			if (target_node != i)
				continue;
		}
		printf("   {\n");
		printf("    \"node_id\" : %d,\n", i);
		printf("    \"devices\" : [");
		for (j = 0; j < nr_cxl_devs; j++) {
			if (cxl_info[j].node_id == i) {
				printf(" \"cxl%d\" ", j);
				devs++;
			}
		}
		printf(" ]\n");
		printf("   }\n");
		node++;
		if (devs == nr_cxl_devs)
			break;
	}
	printf("]\n");
	return EXIT_SUCCESS;

inval_option:
	error("set valid node id \n");
	return -EINVAL;
}

static int add_devs_to_node(int target_node, int nr_devs, const char **devs)
{
	/* add cxl devices in array devs to target node. */
	int ret, i = 0;
	int *dev_list;
	dev_list = malloc(sizeof(int) * nr_devs);
	if (!dev_list)
		return EXIT_FAILURE;
	for (i = 0; i < nr_devs; i++) {
		/* check if devices are valid. */
		if (!sscanf(devs[i], "cxl%d", &dev_list[i])) {
			error("Invalid option. Aborting.\n");
			goto inval_option;
		}
		if (dev_list[i] >= nr_cxl_devs) {
			error("Invalid option. Aborting.\n");
			goto inval_option;
		}
	}
	for (i = 0; i < nr_devs; i++) {
		ret = register_kmem(dev_list[i], target_node);
		if (ret == EXIT_FAILURE)
			goto inval_option;
	}
	free(dev_list);
	return EXIT_SUCCESS;

inval_option:
	free(dev_list);
	return EXIT_FAILURE;
}

static int remove_devs_from_node(int nr_devs, const char **devs)
{
	/* add cxl devices in array devs to target node. */
	int ret, i = 0;
	int *dev_list;
	dev_list = malloc(sizeof(int) * nr_devs);
	if (!dev_list)
		return EXIT_FAILURE;
	for (i = 0; i < nr_devs; i++) {
		/* check if devices are valid. */
		if (!sscanf(devs[i], "cxl%d", &dev_list[i])) {
			error("Invalid option. Aborting.\n");
			goto inval_option;
		}
		if (dev_list[i] >= nr_cxl_devs) {
			error("Invalid option. Aborting.\n");
			goto inval_option;
		}
	}
	for (i = 0; i < nr_devs; i++) {
		ret = dev_change_node(dev_list[i], -1);
		if (ret == EXIT_FAILURE)
			goto inval_option;
	}
	free(dev_list);
	return EXIT_SUCCESS;

inval_option:
	free(dev_list);
	return EXIT_FAILURE;
}

static int remove_node(int target_node)
{
	/* unload every cxl device in target node */
	int i = 0;
	int ret = 0;

	if (target_node > max_node || target_node < 0)
		goto inval_option;

	for (i = 0; i < nr_cxl_devs; i++) {
		if (cxl_info[i].node_id == target_node)
			ret = dev_change_node(i, -1);
		if (ret == EXIT_FAILURE)
			return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;

inval_option:
	error("Invalid option. Aborting.\n");
	return EXIT_FAILURE;
}

double get_read_lat_by_nodes(int node_cpu, int node_mem, unsigned long size,
			     unsigned long stride, int random_access,
			     int num_iter);

static int is_msr_accessible(void)
{
	int fd;
	uint64_t val = 0;
	uint32_t offset = get_msr_offset();

	if ((fd = open("/dev/cpu/0/msr", O_RDWR)) < 0) {
		goto err;
	}
	if (pread(fd, &val, sizeof(val), offset) != sizeof(val)) {
		close(fd);
		goto err;
	}

	if (pwrite(fd, &val, sizeof(val), offset) != sizeof(val)) {
		close(fd);
		goto err;
	}
	close(fd);
	return 1;

err:
	return 0;
}

static int rw_cpus_prefetcher_states(uint64_t *list_val, int is_read)
{
	struct dirent *de;
	struct stat stat_buf;
	DIR *d;
	char filename[MAX_CHAR_LEN];
	uint32_t offset = get_msr_offset();
	if (!offset)
		goto err;

	d = opendir(PATH_MSR);
	if (d) {
		while ((de = readdir(d)) != NULL) {
			if (strcmp(de->d_name, ".") == 0 ||
			    strcmp(de->d_name, "..") == 0)
				continue;
			sprintf(filename, "%s/%s", PATH_MSR, de->d_name);

			if (stat(filename, &stat_buf) == -1)
				goto err;
			if (S_ISDIR(stat_buf.st_mode)) {
				char filename_msr[MAX_CHAR_LEN + 5];
				uint64_t val = 0;
				int cpuid = strtol(de->d_name, NULL, 0);
				int fd;

				sprintf(filename_msr, "%s/msr", filename);
				fd = open(filename_msr, O_RDWR);
				if (fd < 0)
					goto err;

				if (is_read) {
					if (pread(fd, &val, sizeof(val),
						  offset) != sizeof(val)) {
						close(fd);
						goto err;
					}
					list_val[cpuid] = val;
				} else {
					if (list_val[cpuid] == UINT64_MAX) {
						close(fd);
						continue;
					}
					val = list_val[cpuid];

					if (pwrite(fd, &val, sizeof(val),
						   offset) != sizeof(val)) {
						close(fd);
						goto err;
					}
				}
				close(fd);
			}
		}
		closedir(d);
	} else {
		goto err;
	}
	return 0;

err:
	return 1;
}

static uint64_t *orig_val = NULL;
static uint64_t *mod_val = NULL;

static int mod_prefetcher_state(int on)
{
	int num_cpus = sysconf(_SC_NPROCESSORS_CONF);
	orig_val = (uint64_t *)calloc(num_cpus, sizeof(uint64_t));
	if (!orig_val)
		goto err;
	mod_val = (uint64_t *)calloc(num_cpus, sizeof(uint64_t));
	if (!mod_val) {
		free(orig_val);
		goto err;
	}

	for (int i = 0; i < num_cpus; i++)
		orig_val[i] = UINT64_MAX;
	if (rw_cpus_prefetcher_states(orig_val, 1))
		goto err;

	if (on) {
		for (int i = 0; i < num_cpus; i++)
			mod_val[i] = orig_val[i] & (0x0ULL << 0);
	} else {
		for (int i = 0; i < num_cpus; i++)
			mod_val[i] = orig_val[i] | (0xfULL << 0);
	}

	if (rw_cpus_prefetcher_states(mod_val, 0))
		goto err;

	return 0;

err:
	return 1;
}

static void restore_prefetcher_state(void)
{
	if (!orig_val) /* no need to change prefetcher state */
		return;
	if (rw_cpus_prefetcher_states(orig_val, 0))
		error("Cannot change cpus prefetcher state");
	free(orig_val);
	free(mod_val);
	orig_val = mod_val = NULL;

	return;
}

void signal_handler(int sig)
{
	restore_prefetcher_state();
	exit(sig);
}

int get_latency_matrix(int argc, const char **argv)
{
	int num_cpunodes = 0;
	int num_memnodes = 0;
	DIR *d;
	int nodes[MAX_NUMA_NODES] = {
		0,
	};
	int nodes_cpu[MAX_NUMA_NODES] = {
		0,
	};
	int nodes_mem[MAX_NUMA_NODES] = {
		0,
	};
	unsigned long size_mb = 2000;
	unsigned long stride_b = 64;
	int mod_hw_prefetcher = 1;
	int random_access = 0;
	int num_iter = 1;

	/* parse test option */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--size")) {
			if (i + 1 == argc)
				goto inval_option;
			size_mb = strtoul(argv[++i], NULL, 0);
			if (!size_mb)
				goto inval_option;
		} else if (!strcmp(argv[i], "--stride")) {
			if (i + 1 == argc)
				goto inval_option;
			stride_b = strtoul(argv[++i], NULL, 0);
			if (!stride_b)
				goto inval_option;
		} else if (!strcmp(argv[i], "--no-change-prefetcher")) {
			mod_hw_prefetcher = 0;
		} else if (!strcmp(argv[i], "--random")) {
			random_access = 1;
		} else if (!strcmp(argv[i], "--iteration")) {
			if (i + 1 == argc)
				goto inval_option;
			num_iter = strtoul(argv[++i], NULL, 0);
			if (!num_iter)
				goto inval_option;
		} else {
			goto inval_option;
		}
	}
	if (size_mb * 1024 * 1024 < stride_b)
		goto inval_option;

	/* get online nodes and find init/target */
	d = opendir(SYSFS_NODE);
	if (!d) {
		error("no node sysfs found");
	} else {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			int nd, fd, bytes_read;
			char path_cpulist[MAX_CHAR_LEN];
			char buf[4096];
			if (strncmp(de->d_name, "node", 4))
				continue;
			nd = strtol(de->d_name + 4, NULL, 0);
			sprintf(path_cpulist, "%s/node%d/cpulist", SYSFS_NODE,
				nd);
			fd = open(path_cpulist, O_RDONLY);
			if (fd < 0) {
				error("cannot open %s", path_cpulist);
				continue;
			}
			bytes_read = read(fd, buf, 4096);
			if (bytes_read == 1) //mem node
				nodes[nd] = NODE_MEM;
			else if (bytes_read > 0)
				nodes[nd] = NODE_CPU;
			else
				error("cannot read %s", path_cpulist);
			close(fd);
		}
		closedir(d);
	}

	/* handle SIGINT signal */
	signal(SIGINT, signal_handler);

	/* mod prefetcher state */
	if (mod_hw_prefetcher) {
		if (!is_msr_accessible()) {
			printf("Can't modify prefetcher state. Do 'modprobe msr' with root privileges first,"
			       " then run CXL-CLI with root provileges.\n");
			printf("Enable random_acess to get latency matrix"
			       " without hw prefetcher state modification.\n");
			random_access = 1;
			mod_hw_prefetcher = 0;
		} else {
			mod_prefetcher_state(0);
		}
	}

	/* print perf table header */
	printf("\t    Numa node\t\t(unit: ns)\n");
	printf("Numa node\t");
	for (int i = 0; i < MAX_NUMA_NODES; i++) {
		if (!nodes[i])
			continue;
		if (nodes[i] & NODE_CPU)
			nodes_cpu[num_cpunodes++] = i;
		nodes_mem[num_memnodes++] = i;
		printf("%d\t", i);
	}
	printf("\n");

	/* print actual test results for each node */
	for (int i = 0; i < num_cpunodes; i++) {
		double perf;
		printf("\t%d\t", nodes_cpu[i]);
		fflush(stdout);
		for (int j = 0; j < num_memnodes; j++) {
			perf = get_read_lat_by_nodes(nodes_cpu[i], nodes_mem[j],
						     size_mb * 1024 * 1024,
						     stride_b, random_access,
						     num_iter);
			printf("%.1f\t", perf);
			fflush(stdout);
		}
		printf("\n");
	}

	if (mod_hw_prefetcher)
		restore_prefetcher_state();
	return EXIT_SUCCESS;

inval_option:
	error("Invalid option. Aborting.\n");
	print_usage_get_latency_matrix();
	return EXIT_FAILURE;
}

static int read_from_path(const char *path, char *buf, int len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	int n;

	if (fd < 0) {
		error("cannot open %s.\n", path);
		return EXIT_FAILURE;
	}

	n = read(fd, buf, len);
	close(fd);
	if (n < 0) {
		error("Failed to read %s: %s\n", path, strerror(errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static void error_cxlswap_write(void)
{
	printf("\t1. Please check CXLSwap module is installed properly.\n");
	printf("\t2. You need root privileges to control CXLSwap.\n\n");
}

static void error_cxlswap_read(void)
{
	printf("\t1. Please check CXLSwap module is installed properly.\n");
	printf("\t2. You need root privileges to access CXLSwap parameter.\n\n");
}

static void error_cxlswap_usage(void)
{
	printf("\t1. CXLSwap debugfs option must be enabled.\n");
	printf("\t2. You need root privileges to access CXLSwap usage.\n\n");
}

static int control_cxlswap(const char *action)
{
	int ret;
	char status[2];

	/* validate action */
	if (!strcmp(action, "enable"))
		status[0] = '1';
	else if (!strcmp(action, "disable"))
		status[0] = '0';
	else {
		error("  Error: Invalid control: %s\n", action);
		return EXIT_FAILURE;
	}

	/* control cxlswap status */
	ret = write_to_path(SYSFS_CXLSWAP_ENABLED, status);
	if (ret == EXIT_FAILURE) {
		error_cxlswap_write();
		return EXIT_FAILURE;
	}

	printf("Success: CXLSwap is %sd.\n", action);

	return EXIT_SUCCESS;
}

static int check_cxlswap(void)
{
	int ret, fd, n;
	unsigned long long used_bytes, pages;
	char zswap_enabled[] = "/sys/module/zswap/parameters/enabled";
	char cxlswap_used[] = "/sys/kernel/debug/cxlswap/pool_total_size";
	char cxlswap_page[] = "/sys/kernel/debug/cxlswap/stored_pages";
	char status[2];
	char buf[MAX_ULLONG_LEN];
	char *end;

	/* check CXLSwap status */
	ret = read_from_path(SYSFS_CXLSWAP_ENABLED, status, 2);
	if (ret == EXIT_FAILURE) {
		error_cxlswap_read();
		return EXIT_FAILURE;
	}

	if (status[0] == 'Y') {
		/* check zSwap status */
		memset(status, 0, sizeof(char) * 2);
		fd = open(zswap_enabled, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			if (errno == EACCES) {
				error("cannot open %s.\n", zswap_enabled);
				printf("\tCheck privilege to access zSwap parameter.\n");
				printf("\tIf zSwap is enabled, CXLSwap would not be used.\n\n");
			}
		} else {
			n = read(fd, status, 2);
			close(fd);
			if (n < 2) {
				error("Failed to read %s: %s\n", zswap_enabled,
				      strerror(errno));
				printf("\tIf zSwap is enabled, CXLSwap would not be used.\n\n");
			} else if (status[0] == 'Y')
				warning("zSwap: enabled, CXLSwap would not be used.");
		}

		printf("CXLSwap: enabled\n");

		/* read CXLSwap used bytes */
		ret = read_from_path(cxlswap_used, buf, MAX_ULLONG_LEN);
		if (ret == EXIT_FAILURE) {
			error_cxlswap_usage();
			return EXIT_FAILURE;
		}
		used_bytes = strtoull(buf, &end, 10);

		/* read CXLSwap stored pages */
		memset(buf, 0, sizeof(char) * MAX_ULLONG_LEN);
		ret = read_from_path(cxlswap_page, buf, MAX_ULLONG_LEN);
		if (ret == EXIT_FAILURE) {
			error_cxlswap_usage();
			return EXIT_FAILURE;
		}
		pages = strtoull(buf, &end, 10);

		printf("\n");
		printf("CXLSwap Used      : %llu kB\n", used_bytes / 1024);
		printf("CXLSwap Pages     : %llu \n", pages);
	} else if (status[0] == 'N') {
		printf("CXLSwap: disabled\n");
	}

	return EXIT_SUCCESS;
}

static int flush_cxlswap(void)
{
	int ret;
	char status[2];

	/* check CXLSwap status (must be disabled before flush) */
	ret = read_from_path(SYSFS_CXLSWAP_ENABLED, status, 2);
	if (ret == EXIT_FAILURE) {
		error_cxlswap_read();
		return EXIT_FAILURE;
	}
	if (status[0] == 'Y') {
		error("CXLSwap is enabled.\n");
		printf("\tPlease disable CXLSwap before flush.\n\n");
		return EXIT_FAILURE;
	}

	/* flush CXLSwap */
	memset(status, 0, sizeof(char) * 2);
	status[0] = '1';
	ret = write_to_path(SYSFS_CXLSWAP_FLUSH, status);
	if (ret == EXIT_FAILURE) {
		error_cxlswap_write();
		return EXIT_FAILURE;
	}

	printf("Success: CXLSwap is flushed.\n");

	return EXIT_SUCCESS;
}

static void error_cxlcache_write(void)
{
	printf("\t1. Please check CXLCache module is installed properly.\n");
	printf("\t2. You need root privileges to control CXLCache.\n\n");
}

static void error_cxlcache_read(void)
{
	printf("\t1. Please check CXLCache module is installed properly.\n");
	printf("\t2. You need root privileges to access CXLCache parameter.\n\n");
}

static void error_cxlcache_usage(void)
{
	printf("\t1. CXLCache debugfs option must be enabled.\n");
	printf("\t2. You need root privileges to access CXLCache usage.\n\n");
}

static int control_cxlcache(const char *action)
{
	int ret;
	char status[2];

	/* validate action */
	if (!strcmp(action, "enable"))
		status[0] = '1';
	else if (!strcmp(action, "disable"))
		status[0] = '0';
	else {
		error("  Error: Invalid control: %s\n", action);
		return EXIT_FAILURE;
	}

	/* control cxlcache status */
	ret = write_to_path(SYSFS_CXLCACHE_ENABLED, status);
	if (ret == EXIT_FAILURE) {
		error_cxlcache_write();
		return EXIT_FAILURE;
	}

	printf("Success: CXLCache is %sd.\n", action);

	return EXIT_SUCCESS;
}

static int check_cxlcache(void)
{
	int ret;
	unsigned long long cache_used, cache_pages;
	char cxlcache_used[] = "/sys/kernel/debug/cxlcache/pool_total_size";
	char cxlcache_pages[] = "/sys/kernel/debug/cxlcache/put_pages";
	char status[2];
	char buf[MAX_ULLONG_LEN];
	char *end;

	/* check CXLcache status */
	ret = read_from_path(SYSFS_CXLCACHE_ENABLED, status, 2);
	if (ret == EXIT_FAILURE) {
		error_cxlcache_read();
		return EXIT_FAILURE;
	}

	if (status[0] == 'Y') {
		printf("CXLCache: enabled\n");

		/* read CXLcache Used */
		ret = read_from_path(cxlcache_used, buf, MAX_ULLONG_LEN);
		if (ret == EXIT_FAILURE)
			goto err;
		cache_used = strtoull(buf, &end, 10);

		/* read CXLcache Num Pages */
		memset(buf, 0, sizeof(char) * MAX_ULLONG_LEN);
		ret = read_from_path(cxlcache_pages, buf, MAX_ULLONG_LEN);
		if (ret == EXIT_FAILURE)
			goto err;
		cache_pages = strtoull(buf, &end, 10);

		printf("\n");
		printf("CXLCache Used      : %llu kB\n", cache_used / 1024);
		printf("CXLCache Pages     : %llu \n", cache_pages);
	} else if (status[0] == 'N') {
		printf("CXLCache: disabled\n");
	}

	return EXIT_SUCCESS;

err:
	error_cxlcache_usage();
	return EXIT_FAILURE;
}

static int flush_cxlcache(void)
{
	int ret;
	char status[2];

	/* check CXLCache status (must be disabled before flush) */
	ret = read_from_path(SYSFS_CXLCACHE_ENABLED, status, 2);
	if (ret == EXIT_FAILURE) {
		error_cxlcache_read();
		return EXIT_FAILURE;
	}

	if (status[0] == 'Y') {
		error("CXLCache is enabled.\n");
		printf("\tPlease disable CXLCache before flush.\n\n");
		return EXIT_FAILURE;
	}

	/* flush CXLCache */
	memset(status, 0, sizeof(char) * 2);
	status[0] = '1';
	ret = write_to_path(SYSFS_CXLCACHE_FLUSH, status);
	if (ret == EXIT_FAILURE) {
		error_cxlcache_write();
		return EXIT_FAILURE;
	}

	printf("Success: CXLCache is flushed.\n");

	return EXIT_SUCCESS;
}

static int _soft_interleaving_group_add(int nr_devs, const char **devs,
					int target_node)
{
	if (target_node > max_node || target_node < 0)
		goto inval_option;

	return add_devs_to_node(target_node, nr_devs, devs);

inval_option:
	error("Invalid option. Aborting.\n");
	return EXIT_FAILURE;
}

static int _soft_interleaving_group_remove(int nr_devs, const char **devs,
					   int target_node)
{
	if (target_node == -1)
		return remove_devs_from_node(nr_devs, devs);
	else
		return remove_node(target_node);
}

int soft_interleaving_group_add(int argc, const char **argv, int target_node,
				int *count)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = _soft_interleaving_group_add(argc, argv, target_node);
	finalize_cxl_dev_info();
	if (!ret)
		*count = 1;
	return ret;
}

int soft_interleaving_group_remove(int argc, const char **argv, int target_node,
				   int *count)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = _soft_interleaving_group_remove(argc, argv, target_node);
	finalize_cxl_dev_info();
	if (!ret)
		*count = 1;
	return ret;
}

int soft_interleaving_group_node(int *count)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = mode_node(count);
	finalize_cxl_dev_info();
	return ret;
}

int soft_interleaving_group_noop(int *count)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = mode_noop(count);
	finalize_cxl_dev_info();
	return ret;
}

int soft_interleaving_group_list_node(int target)
{
	int ret = 0;

	init_cxl_dev_info();
	if (target != -2)
		ret = print_cxl_info_node(target);
	else
		ret = print_cxl_info_node(PRINT_EVERY_NODE);
	finalize_cxl_dev_info();
	return ret;
}

int soft_interleaving_group_list_dev(char *dev)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = print_cxl_info_dev(dev);
	finalize_cxl_dev_info();
	return ret;
}

int cmd_get_latency_matrix(int argc, const char **argv, struct cxl_ctx *ctx)
{
	return get_latency_matrix(argc, argv);
}

int cmd_disable_cxlswap(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = control_cxlswap("disable");
	return ret;
}

int cmd_enable_cxlswap(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = control_cxlswap("enable");
	return ret;
}

int cmd_check_cxlswap(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = check_cxlswap();
	return ret;
}

int cmd_flush_cxlswap(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = flush_cxlswap();
	return ret;
}

int cmd_disable_cxlcache(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = control_cxlcache("disable");
	return ret;
}

int cmd_enable_cxlcache(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = control_cxlcache("enable");
	return ret;
}

int cmd_check_cxlcache(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = check_cxlcache();
	return ret;
}

int cmd_flush_cxlcache(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	ret = flush_cxlcache();
	return ret;
}
