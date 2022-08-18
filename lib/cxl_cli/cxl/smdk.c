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

#define SYSFS_DAX_BIND "/sys/bus/dax/drivers/device_dax/bind"
#define SYSFS_DAX_UNBIND "/sys/bus/dax/drivers/device_dax/unbind"

#define SYSFS_CXL_DEVICES "/sys/bus/cxl/devices"
#define SYSFS_CXL_NODE "/sys/bus/cxl/nodes"
#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"
#define SYSFS_NODE_POSSIBLE "/sys/devices/system/node/possible"
#define SYSFS_NODE_HAS_CPU "/sys/devices/system/node/has_cpu"

#define IOMEM "/proc/iomem"
#define MAX_CHAR_LEN 200
#define PRINT_EVERY_NODE -2
#define PCI_INFO_LEN 16

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


static int arg_to_int(const char *s, void (*fp)(void))
{
	int i = 0;
	int size = strlen(s);

	if (size == 0)
		return 0;

	for (i = 0; i < size; i++) {
		if (!((s[i] >= '0' && s[i] <= '9') || s[i] == '-')){
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
	if (!fp){
		error("cannot open %s.\nAborting.", SYSFS_NODE_POSSIBLE);
		exit(1);
	}
	if (fgets(str, 10, fp) == NULL) {
		error("cannot get content of %s.\nAborting.", 
		      SYSFS_NODE_POSSIBLE);
		fclose(fp);
		exit(1);
	}

	if (!sscanf(str, "0-%d", &max_node_parse)){
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
	if (!sscanf(str, "0-%d", &nr_node_cpu)){
		fclose(fp);
		return 1;
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

	for(i = 0; i < nr_cxl_devs; i++) {
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
	char map_path[MAX_CHAR_LEN], buf[MAX_CHAR_LEN];
	char range[MAX_CHAR_LEN];

	sprintf(range, "%lx-%lx", start, start + size - 1);
	sprintf(map_path, "/sys/devices/platform/hmem.%d/dax%d.0/mapping",
			dev, dev);
	if (access(map_path, W_OK)) {
		error("%s is not accessible.\n", map_path);
		return EXIT_FAILURE;
	}
	sprintf(buf, "%s", range);

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
	for(i = 0; i < n_target_devs; i++) {
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
	for(i = 0; i < nr_cxl_devs; i++) {
		if (dev_change_node(i, cxl_info[i].socket_id) == EXIT_FAILURE)
			return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int mode_node(void)
{
	int i = 0;
	int target_node = nr_socket;
	if (unbind_all() == EXIT_FAILURE)
		return EXIT_FAILURE;
	for(i = 0; i < nr_cxl_devs; i++) {
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
	for(i = 0; i < nr_cxl_devs; i++) {
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
	fprintf(stderr, "*** cxl group-dax cmd usage ***\n\n");
	fprintf(stderr, "\tcxl group-dax [--dev <cxl0> [<cxl1>..<cxlN>]]\n");
	fprintf(stderr, "\t\t : Converts designated cxl device(s) into dax device(s).\n");
	fprintf(stderr, "\t\t   When the list of devices is not set, all cxl devices are converted into dax devices.\n");
	fprintf(stderr, "\t\t   ex) cxl group-dax --dev\n");
	fprintf(stderr, "\t\t   ex) cxl group-dax --dev cxl0 cxl2\n\n");
	return;
}

static void print_usage_group_add(void)
{
	fprintf(stderr, "*** cxl group-add cmd usage ***\n\n");
	fprintf(stderr, "\tcxl group-add --target_node <node_id> --dev <cxl0> [<cxl1>..<cxlN>]\n");
	fprintf(stderr, "\t\t : Adds designated device(s) to target node.\n");
	fprintf(stderr, "\t\t   If the device(s) is already included in another node, it is automatically excluded from that node.\n");
	fprintf(stderr, "\t\t   ex) cxl group-add --target_node 1 --dev cxl0 cxl2\n\n");
	return;
}

static void print_usage_group_remove(void)
{
	fprintf(stderr, "*** cxl group-remove cmd usage ***\n\n");
	fprintf(stderr, "\tcxl group-remove --dev <cxl0> [<cxl1>..<cxlN>]\n");
	fprintf(stderr, "\t\t : Removes designated device(s) from its node, and make the device offline.\n");
	fprintf(stderr, "\t\t   ex) cxl group-remove --dev cxl1 cxl2 cxl4\n");
	fprintf(stderr, "\tcxl group-remove --node <node_id>\n");
	fprintf(stderr, "\t\t : Removes all cxl devices from specified node.\n");
	fprintf(stderr, "\t\t   ex) cxl group-remove --node 1\n\n");
	return;
}

static void print_usage_group_list(void)
{
	fprintf(stderr, "*** cxl group-list cmd usage ***\n\n");
	fprintf(stderr, "\tcxl group-list --dev [<cxlN>]\n");
	fprintf(stderr, "\t\t : Displays information of designated cxl device.\n");
	fprintf(stderr, "\t\t   If there is no specified device, this command shows information of all cxl devices in the system.\n");
	fprintf(stderr, "\t\t   ex) cxl group-list --dev\n");
	fprintf(stderr, "\t\t   ex) cxl group-list --dev cxl2\n");
	fprintf(stderr, "\tcxl group-list --node [<node_id>]\n");
	fprintf(stderr, "\t\t : Displays information about node-grouping status of designated node.\n");
	fprintf(stderr, "\t\t   If there is no specified  node, this command shows information of all nodes in the system.\n");
	fprintf(stderr, "\t\t   ex) cxl group-list --node\n");
	fprintf(stderr, "\t\t   ex) cxl group-list --node 1\n\n");
	return;
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
		for(i = 0; i < nr_cxl_devs; i++)
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
	for(i = -1; i <= max_node; i++){
		if (target_node != PRINT_EVERY_NODE){
			if (target_node != i)
				continue;
		}
		printf("   {\n");
		printf("    \"node_id\" : %d,\n", i);
		printf("    \"devices\" : [");
		for(j = 0; j < nr_cxl_devs; j++){
			if (cxl_info[j].node_id == i){
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
	for(i = 0; i < nr_devs; i++){
		/* check if devices are valid. */
		if(!sscanf(devs[i], "cxl%d", &dev_list[i])) {
			error("Invalid option. Aborting.\n");
			goto inval_option;
		}
		if (dev_list[i] >= nr_cxl_devs){
			error("Invalid option. Aborting.\n");
			goto inval_option;
		}
	}
	for(i = 0; i < nr_devs; i++){
		if (is_dev_dax(dev_list[i])){
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
	for(i = 0; i < nr_cxl_devs; i++){
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
	else if (!strcmp(argv[1], "--node")){
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

