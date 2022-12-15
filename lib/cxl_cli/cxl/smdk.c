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

#define IOMEM "/proc/iomem"
#define SYSFS_DAX_BIND "/sys/bus/dax/drivers/device_dax/bind"
#define SYSFS_DAX_UNBIND "/sys/bus/dax/drivers/device_dax/unbind"
#define SYSFS_DAX_MAP "/sys/devices/platform/hmem.%d/dax%d.0/mapping"
#define SYSFS_DAX_MAP0 "/sys/devices/platform/hmem.%d/dax%d.0/mapping0"

#define SYSFS_CXL_DEVICES "/sys/kernel/cxl/devices"
#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"

#define SYSFS_NODE "/sys/devices/system/node/"
#define SYSFS_NODE_POSSIBLE "/sys/devices/system/node/possible"
#define SYSFS_NODE_HAS_CPU "/sys/devices/system/node/has_cpu"

#define SYSFS_CXLSWAP_ENABLED "/sys/module/cxlswap/parameters/enabled"
#define SYSFS_CXLSWAP_FLUSH "/sys/module/cxlswap/parameters/flush"

#define MAX_NUMA_NODES (64)
#define MAX_CHAR_LEN (1024)
#define PCI_INFO_LEN (16)
#define MAX_ULLONG_LEN (32)

#define PRINT_EVERY_NODE (-2)

#define HWP_ENABLE (0x0)
#define HWP_DISABLE (0xF)
#define PATH_MSR "/dev/cpu"

#define NODE_CPU (1 << 0)
#define NODE_MEM (1 << 2)

#define OFFSET_MODEL (4)
#define OFFSET_EXT_MODEL (16)


struct cxl_dev_info {
	int num;
	int node_id;
	int socket_id;
	size_t start;
	size_t size;
	int state;
	int memdev;
	char pci_bus_addr[PCI_INFO_LEN];
	char pci_cur_link_speed[PCI_INFO_LEN];
	char pci_cur_link_width[PCI_INFO_LEN];
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
	char vendor[13] = {0,};
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
		if (model == 0xf || model == 0x1d || model == 0x16 || model == 0x17 ) /* core 2 family */
			return 0x1a0;
		else
			return 0x1a4;
	} else if (!strncmp(vendor, "AuthenticAMD", 12)) {
		return 0xc0000108;
	} else {
		return 0;
	}
}

static int arg_to_int(const char *s, void (*fp)(void))
{
	int i = 0;
	int size = strlen(s);

	if (size == 0)
		return 0;

	for (i = 0; i < size; i++) {
		if (!((s[i] >= '0' && s[i] <= '9') || s[i] == '-')) {
			error("Invalid option. Aborting.\n");
			fp();
			exit(1);
		}
	}
	return atoi(s);
}

static int get_system_max_node(void)
{
	int max_node_parse = 0;
	char str[10];
	FILE* fp;
	fp = fopen(SYSFS_NODE_POSSIBLE, "r");
	if (!fp) {
		error("cannot open %s.\nAborting.", SYSFS_NODE_POSSIBLE);
		exit(1);
	}
	if (fgets(str, 10, fp) == NULL) {
		error("cannot get content of %s.\nAborting.", 
			SYSFS_NODE_POSSIBLE);
		fclose(fp);
		exit(1);
	}

	if (!sscanf(str, "0-%d", &max_node_parse)) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	return max_node_parse;
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
	FILE* fp;
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
	FILE* fp;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/state", dev);
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
	FILE* fp;

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
	FILE* fp;

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
	FILE* fp;

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
	start_addr = (size_t)strtol(str+2, NULL, 16);
	return start_addr;
}

static size_t get_dev_size(int dev)
{
	char path[MAX_CHAR_LEN];
	char str[MAX_CHAR_LEN];
	size_t size;
	FILE* fp;

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
	size = (size_t)strtol(str+2, NULL, 16);
	return size;
}

static int get_dev_memdev(int dev)
{
	DIR *d;
	struct dirent *de;
	char path[MAX_CHAR_LEN];
	int memdev_id = -1;

	sprintf(path, "/sys/kernel/cxl/devices/cxl%d/", dev);
	d = opendir(path);
	if (d) {
		char *endptr;
		while ((de = readdir(d)) != NULL) {
			if (strncmp(de->d_name, "mem", 3))
				continue;
			memdev_id = strtol(de->d_name + 3, &endptr, 0);
			if (endptr == (de->d_name + 3)) //no conversion case
				continue;
			break;
		}
		closedir(d);
	}
	return memdev_id;
}

static void get_dev_pci_info(struct cxl_dev_info *dev)
{
	char path_symlink[MAX_CHAR_LEN] = {0,};
	char path_bus_addr[MAX_CHAR_LEN] = {0,};
	char path_link_speed[MAX_CHAR_LEN] = {0,};
	char path_link_width[MAX_CHAR_LEN] = {0,};
	char *tok;
	FILE *fp;
	size_t read_len, len = 0;
	int sz_link;

	/* get pci bus addr */
	sprintf(path_symlink, "/sys/kernel/cxl/devices/cxl%d/mem%d",
			      dev->num, dev->memdev);
	sz_link = readlink(path_symlink, path_bus_addr, MAX_CHAR_LEN);
	if (sz_link < 0)
		return;
	tok = strtok(path_bus_addr, "/");
	while (tok != NULL && len != 12) {
		tok = strtok(NULL, "/");
		len = strlen(tok);
	}
	strcpy(dev->pci_bus_addr, tok);

	/* get link speed */
	sprintf(path_link_speed, "%s/%s/current_link_speed",
				 SYSFS_PCI_DEVICES, dev->pci_bus_addr);
	if (!access(path_link_speed, R_OK)) {
		fp = fopen(path_link_speed, "r");
		if (fp) {
			read_len = fread(dev->pci_cur_link_speed, 1, PCI_INFO_LEN, fp);
			dev->pci_cur_link_speed[read_len - 1] = (char)0; //remove newline
			fclose(fp);
		}
	}

	/* get link width */
	sprintf(path_link_width, "%s/%s/current_link_width",
				 SYSFS_PCI_DEVICES, dev->pci_bus_addr);
	if (!access(path_link_width, R_OK)) {
		fp = fopen(path_link_width, "r");
		if (fp) {
			read_len = fread(dev->pci_cur_link_width, 1, PCI_INFO_LEN, fp);
			dev->pci_cur_link_width[read_len - 1] = (char)0; //remove newline
			if (strcmp(dev->pci_cur_link_width, "0") == 0)
				sprintf(dev->pci_cur_link_width, "Unknown");
			fclose(fp);
		}
	}
}

static void init_cxl_dev_info(void)
{
	int i;
	nr_cxl_devs = get_nr_cxl_devs();
	nr_socket = get_nr_socket();
	max_node = get_system_max_node();
	cxl_info = calloc(nr_cxl_devs, sizeof(struct cxl_dev_info));

	for (i = 0; i < nr_cxl_devs; i++) {
		cxl_info[i].num = i;
		cxl_info[i].node_id = get_dev_node(i);
		cxl_info[i].socket_id = get_dev_socket(i);
		cxl_info[i].state = get_dev_state(i);
		cxl_info[i].start = get_dev_start_addr(i);
		cxl_info[i].size = get_dev_size(i);
		cxl_info[i].memdev = get_dev_memdev(i);
		get_dev_pci_info(&cxl_info[i]);
	}
}

static bool is_dev_dax(int dev)
{
	/* read /proc/iomem , check if dev is in dax mode */
	char str[MAX_CHAR_LEN];
	char devname[MAX_CHAR_LEN];
	char *tok;
	FILE *fp;

	sprintf(devname, "dax%d.0", dev);
	if (access(IOMEM, R_OK)) {
		error("%s is not accessible.\nAborting.", IOMEM);
		exit(1);
	}
	fp = fopen(IOMEM, "r");
	while(1) {
		if (fgets(str, MAX_CHAR_LEN, fp) == NULL) {
			break;
		}
		tok = strtok(str, " ");
		tok = strtok(NULL, " ");
		tok = strtok(NULL, "\n");
		if (!strcmp(tok, devname)) {
			fclose(fp);
			return true;
		}
	}
	fclose(fp);
	return false;
}

static int write_to_path(const char *path, const char *buf)
{
	int fd = open(path, O_WRONLY|O_CLOEXEC);
	int n, len = strlen(buf) + 1;

	if (fd < 0) {
		error("cannot open %s.\n", path);
		return EXIT_FAILURE;
	}

	n = write(fd, buf, len);
	close(fd);
	if (n < len) {
		error("Failed to write %s to %s: %s\n", buf, path,
				strerror(errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
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

static int dax_map(int dev, size_t start, size_t size)
{
	char map_path[MAX_CHAR_LEN], map0_path[MAX_CHAR_LEN];
	char range[MAX_CHAR_LEN];

	sprintf(map0_path, SYSFS_DAX_MAP0, dev, dev);
	if (!access(map0_path, F_OK)) {
		/* mapped already */
		return EXIT_SUCCESS;
	}

	sprintf(map_path, SYSFS_DAX_MAP, dev, dev);
	if (access(map_path, W_OK)) {
		error("%s is not accessible.\n", map_path);
		return EXIT_FAILURE;
	}

	sprintf(range, "%lx-%lx", start, start + size - 1);
	return write_to_path(map_path, range);
}

static int dax_bind(int dev)
{
	char devname[MAX_CHAR_LEN];

	if (access(SYSFS_DAX_BIND, W_OK)) {
		error("%s is not accessible.\n", SYSFS_DAX_BIND);
		return EXIT_FAILURE;
	}
	sprintf(devname, "dax%d.0", dev);

	return write_to_path(SYSFS_DAX_BIND, devname);
}

static int dax_unbind(int dev)
{
	char devname[MAX_CHAR_LEN];

	if (access(SYSFS_DAX_UNBIND, W_OK)) {
		error("%s is not accessible.\n", SYSFS_DAX_UNBIND);
		return EXIT_FAILURE;
	}
	sprintf(devname, "dax%d.0", dev);

	return write_to_path(SYSFS_DAX_UNBIND, devname);
}

static int register_dev_dax(int dev)
{
	int ret = 0;;

	ret = dev_change_node(dev, -1);
	if (ret == EXIT_FAILURE)
		return EXIT_FAILURE;
	ret = dax_map(dev, cxl_info[dev].start, cxl_info[dev].size);
	if (ret == EXIT_FAILURE)
		return EXIT_FAILURE;
	ret = dax_bind(dev);
	return ret;
}

static int unbind_all(void)
{
	int i = 0;

	for (i = 0; i < nr_cxl_devs; i++) {
		if (is_dev_dax(i)) {
			if (dax_unbind(i) == EXIT_FAILURE)
				return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int mode_dax(int* devs, int n_target_devs)
{
	/* register devices in array devs to dax */
	int i;
	int ret = 0;
	for (i = 0; i < n_target_devs; i++) {
		/* if device is already dax, jump to next dev*/
		if (is_dev_dax(devs[i]))
			continue;
		ret = register_dev_dax(devs[i]);
		if (ret == EXIT_FAILURE)
			return ret;
	}
	return EXIT_SUCCESS;
}

static int mode_zone(void)
{
	int i = 0;
	if (unbind_all() == EXIT_FAILURE)
		return EXIT_FAILURE;
	for (i = 0; i < nr_cxl_devs; i++) {
		if (dev_change_node(i, cxl_info[i].socket_id) == EXIT_FAILURE)
			return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int mode_node(void)
{
	int target_node, i = 0;
	if (unbind_all() == EXIT_FAILURE)
		return EXIT_FAILURE;
	for (i = 0; i < nr_cxl_devs; i++) {
		target_node = nr_socket + cxl_info[i].socket_id;
		if (dev_change_node(i, target_node) == EXIT_FAILURE)
			return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int mode_noop(void)
{
	int i = 0;
	int target_node = nr_socket;
	if (unbind_all() == EXIT_FAILURE)
		return EXIT_FAILURE;
	for (i = 0; i < nr_cxl_devs; i++) {
		if (dev_change_node(i, target_node) == EXIT_FAILURE)
			return EXIT_FAILURE;
		target_node++;
	}
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
	printf("      \"memdev\":\"%d\",\n", cxl_info[dev_id].memdev);
	printf("      \"pci_bus_addr\":\"%s\",\n", cxl_info[dev_id].pci_bus_addr);
	printf("      \"pci_cur_link_speed\":\"%s\",\n", cxl_info[dev_id].pci_cur_link_speed);
	printf("      \"pci_cur_link_width\":\"%s\",\n", cxl_info[dev_id].pci_cur_link_width);
	if (cxl_info[dev_id].state == 1)
		printf("      \"state\":\"online\",\n");
	else if (cxl_info[dev_id].state == 0)
		printf("      \"state\":\"offline\",\n");
	else
		printf("      \"state\":\"error\",\n");
	printf("   }\n");
}

static void print_usage_group_dax(void)
{
	printf("*** cxl group-dax cmd usage ***\n\n");
	printf("\tcxl group-dax [--dev <cxl0> [<cxl1>..<cxlN>]]\n");
	printf("\t\t : Converts designated cxl device(s) into dax device(s).\n");
	printf("\t\t   When the list of devices is not set, all cxl devices are converted into dax devices.\n");
	printf("\t\t   ex) cxl group-dax\n");
	printf("\t\t   ex) cxl group-dax --dev cxl0 cxl2\n\n");
	return;
}

static void print_usage_group_add(void)
{
	printf("*** cxl group-add cmd usage ***\n\n");
	printf("\tcxl group-add --target_node <node_id> --dev <cxl0> [<cxl1>..<cxlN>]\n");
	printf("\t\t : Adds designated device(s) to target node.\n");
	printf("\t\t   If the device(s) is already included in another node, it is automatically excluded from that node.\n");
	printf("\t\t   ex) cxl group-add --target_node 1 --dev cxl0 cxl2\n\n");
	return;
}

static void print_usage_group_remove(void)
{
	printf("*** cxl group-remove cmd usage ***\n\n");
	printf("\tcxl group-remove --dev <cxl0> [<cxl1>..<cxlN>]\n");
	printf("\t\t : Removes designated device(s) from its node, and make the device offline.\n");
	printf("\t\t   ex) cxl group-remove --dev cxl1 cxl2 cxl4\n");
	printf("\tcxl group-remove --node <node_id>\n");
	printf("\t\t : Removes all cxl devices from specified node.\n");
	printf("\t\t   ex) cxl group-remove --node 1\n\n");
	return;
}

static void print_usage_group_list(void)
{
	printf("*** cxl group-list cmd usage ***\n\n");
	printf("\tcxl group-list --dev [<cxlN>]\n");
	printf("\t\t : Displays information of designated cxl device.\n");
	printf("\t\t   If there is no specified device, this command shows information of all cxl devices in the system.\n");
	printf("\t\t   ex) cxl group-list --dev\n");
	printf("\t\t   ex) cxl group-list --dev cxl2\n");
	printf("\tcxl group-list --node [<node_id>]\n");
	printf("\t\t : Displays information about node-grouping status of designated node.\n");
	printf("\t\t   If there is no specified  node, this command shows information of all nodes in the system.\n");
	printf("\t\t   ex) cxl group-list --node\n");
	printf("\t\t   ex) cxl group-list --node 1\n\n");
	return;
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

static int print_cxl_info_dev(int argc, const char **argv)
{
	int i;
	int dev_id;
	if (argc > 2)
		goto inval_option;
	if (argc == 2) {
		if ((!sscanf(argv[1], "cxl%d", &dev_id)) ||
				(dev_id < 0 || dev_id >= nr_cxl_devs))
			goto inval_option;
	}

	printf("[\n");
	if (argc == 2) {
		if (dev_id < nr_cxl_devs && dev_id >= 0)
			_print_cxl_info_dev(dev_id);
	} else {
		for (i = 0; i < nr_cxl_devs; i++)
			_print_cxl_info_dev(i);
	}
	printf("]\n");
	return EXIT_SUCCESS;

inval_option:
	error("Invalid option. Aborting.\n");
	print_usage_group_list();
	return EXIT_FAILURE;
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
				devs ++;
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
	error("Invalid Node. Aborting.\n");
	print_usage_group_list();
	return EXIT_FAILURE;
}

static int group_dax(int argc, const char **argv)
{
	/*
	 * If target mode is dax,
	 * more args(target dev) should be handled
	 */
	int i = 0;
	int ret = 0;
	int n_target_devs = 0;
	int *target_devs;

	/*
	 * If a user designates specific devices to convert into dax,
	 * register the devices to target_dev_array.
	 * Meanwhile, check if the devices are valid.
	 */
	if ((argc > 2) && !strcmp(argv[1], "--dev")) {
		n_target_devs = argc - 2;
		target_devs = malloc(sizeof(int) * n_target_devs);
		for (i = 0; i < n_target_devs; i++) {
			if (!sscanf(argv[i+2], "cxl%d", &target_devs[i]))
				goto inval_option;
			if (target_devs[i] >= nr_cxl_devs)
				goto inval_option;
		}
		ret = mode_dax(target_devs, n_target_devs);
	} else if (argc == 1) {
		/* register all devices to target_device_array */
		n_target_devs = nr_cxl_devs;
		target_devs = malloc(sizeof(int) * n_target_devs);
		for (i = 0; i < n_target_devs; i++)
			target_devs[i] = i;
		ret = mode_dax(target_devs, n_target_devs);
	} else {
		error("Invalid option. Aborting.\n");
		print_usage_group_dax();
		return EXIT_FAILURE;
	}
	free(target_devs);
	return ret;

inval_option:
	error("Invalid option. Aborting.\n");
	print_usage_group_dax();
	free(target_devs);
	return EXIT_FAILURE;
}

static int add_devs_to_node(int target_node, int nr_devs, const char **devs)
{
	/* add cxl devices in array devs to target node. */
	int i = 0;
	int *dev_list;
	dev_list = malloc(sizeof(int) * nr_devs);
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
		if (is_dev_dax(dev_list[i])) {
			/* if device is dax device, unbind. */
			if (dax_unbind(dev_list[i]) == EXIT_FAILURE)
				goto inval_option;
		}
		if (dev_change_node(dev_list[i], target_node) == EXIT_FAILURE)
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
	for (i = 0; i < nr_cxl_devs; i++) {
		if (cxl_info[i].node_id == target_node)
			ret = dev_change_node(i, -1);
		if (ret == EXIT_FAILURE)
			return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int group_add(int argc, const char **argv)
{
	int ret = 0;
	int target_node = -2;
	int nr_devs = 0;

	if (argc < 5)
		goto inval_option;
	if (!strcmp(argv[1], "--target_node"))
		target_node = arg_to_int(argv[2], print_usage_group_add);
	else
		goto inval_option;

	if (target_node > max_node || target_node < -1)
		goto inval_option;
	if (!strcmp(argv[3], "--dev")) {
		nr_devs = argc - 4;
		ret = add_devs_to_node(target_node, nr_devs, argv + 4);
		if (ret == EXIT_FAILURE)
			print_usage_group_add();
	} else {
		goto inval_option;
	}
	return ret;

inval_option:
	error("Invalid option. Aborting.\n");
	print_usage_group_add();
	return EXIT_FAILURE;
}

int group_remove(int argc, const char **argv)
{
	int ret = 0;
	int target_node = -1;
	int nr_devs = 0;

	if (argc < 3)
		goto inval_option;
	if (!strcmp(argv[1], "--dev")) {
		nr_devs = argc - 2;
		ret = add_devs_to_node(target_node, nr_devs, argv + 2);
		if (ret)
			print_usage_group_remove();
	} else if (!strcmp(argv[1], "--node")) {
		target_node = arg_to_int(argv[2], print_usage_group_remove);
		if (target_node < -1 || target_node > max_node)
			goto inval_option;
		remove_node(target_node);
	} else {
		goto inval_option;
	}
	return ret;

inval_option:
	error("Invalid option. Aborting.\n");
	print_usage_group_remove();
	return EXIT_FAILURE;
}

int group_list(int argc, const char **argv)
{
	int ret = 0;
	int target = 0;

	if (argc < 2)
		goto inval_option;
	if (!strcmp(argv[1], "--dev"))
		ret = print_cxl_info_dev(argc - 1, argv + 1);
	else if (!strcmp(argv[1], "--node")) {
		if (argc == 2)
			ret = print_cxl_info_node(PRINT_EVERY_NODE);
		else if (argc == 3) {
			target = arg_to_int(argv[2], print_usage_group_list);
			if (target > max_node || target < -1)
				goto inval_option;
			ret = print_cxl_info_node(target);
		} else
			goto inval_option;
	} else {
		goto inval_option;
	}
	return ret;

inval_option:
	error("Invalid option. Aborting.\n");
	print_usage_group_list();
	return EXIT_FAILURE;
}

double get_read_lat_by_nodes(int node_cpu, int node_mem, unsigned long size,
				unsigned long stride, int random_access, int num_iter);

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

	if (pwrite(fd, &val, sizeof(val), offset)!= sizeof(val)) {
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
			if (strcmp(de->d_name, ".") == 0
				|| strcmp(de->d_name, "..") == 0)
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
					if (pread(fd, &val, sizeof(val), offset)
							!= sizeof(val)) {
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

					if (pwrite(fd, &val, sizeof(val), offset)
							!= sizeof(val)) {
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
	int nodes[MAX_NUMA_NODES] = {0,};
	int nodes_cpu[MAX_NUMA_NODES] = {0,};
	int nodes_mem[MAX_NUMA_NODES] = {0,};
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
			sprintf(path_cpulist, "%s/node%d/cpulist", SYSFS_NODE, nd);
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
						size_mb * 1024 * 1024, stride_b,
						random_access, num_iter);
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
	int fd = open(path, O_RDONLY|O_CLOEXEC);
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
	printf("\t2. Root privilege is required to control CXLSwap.\n\n");
}

static void error_cxlswap_read(void)
{
	printf("\t1. Please check CXLSwap module is installed properly.\n");
	printf("\t2. Check privilege to access CXLSwap parameter.\n\n");
}

static void error_cxlswap_usage(void)
{
	printf("\t1. CXLSwap debugfs option must be enabled.\n");
	printf("\t2. Root privilege is required to access CXLSwap usage.\n\n");
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
		fd = open(zswap_enabled, O_RDONLY|O_CLOEXEC);
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
				error("Failed to read %s: %s\n", zswap_enabled, strerror(errno));
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
		printf("CXLSwap Used      : %llu kB\n", used_bytes/1024);
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

int cmd_group_zone(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	init_cxl_dev_info();
	ret = mode_zone();
	free(cxl_info);
	return ret;
}

int cmd_group_node(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	init_cxl_dev_info();
	ret = mode_node();
	free(cxl_info);
	return ret;
}

int cmd_group_noop(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	if (argc > 1)
		error("This CMD does not require additional param(s)");

	init_cxl_dev_info();
	ret = mode_noop();
	free(cxl_info);
	return ret;
}

int cmd_group_dax(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = group_dax(argc, argv);
	free(cxl_info);
	return ret;
}

int cmd_group_list(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = group_list(argc, argv);
	free(cxl_info);
	return ret;
}

int cmd_group_add(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = group_add(argc, argv);
	free(cxl_info);
	return ret;
}

int cmd_group_remove(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int ret = 0;

	init_cxl_dev_info();
	ret = group_remove(argc, argv);
	free(cxl_info);
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

