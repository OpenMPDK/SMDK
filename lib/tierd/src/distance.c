#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <numa.h>
#include <numaif.h>
#include "common.h"
#include "distance.h"

/*
 * Making node fallback list for Adaptive Interleaving and SMDK Allocator.
 */

/*
 * If the difference of two nodes' bandwidth is below N percentage,
 * they are treated as the same tier.
 */
#define TOLERANCE 5.0f // In %

typedef struct __node_info {
	int nid;
	int val;
} NodeInfo;

typedef struct __Cluster {
	int num_nodes;
	NodeInfo *nodes;
} Cluster;

static int compare(const void *_a, const void *_b)
{
	NodeInfo *a = (NodeInfo *)_a;
	NodeInfo *b = (NodeInfo *)_b;
	if (a->val == b->val)
		return 0;
	else if (a->val > b->val)
		return 1;
	return -1;
}

static int write_to_order_file(NodeInfo *list, int sid,
		int start, int end, char *name)
{
	for (int i = 0; i < fallback_order_f_num; i++) {
		if (!strcmp(name, fallback_order_f[i])) {
			char file_path[MAX_PATH_LEN];
			sprintf(file_path, "%s/%s", node_dir_path[sid], name);

			FILE *fd = fopen(file_path, "w");
			if (fd == NULL) {
				tierd_error("Fail to open: %s\n", file_path);
				return -1;
			}

			while (1) {
				if (start == end) {
					fprintf(fd, "\n");
					break;
				}
				fprintf(fd, start + 1 == end ? "%d" : "%d,",
						list[start].nid);
				start += 1;
			}

			fclose(fd);
			break;
		}
	}

	return 0;
}

static int update_nodes_fallback_by_tier()
{
	int ret = 0;
	int dram_nodes_num = total_sockets;
	int ext_nodes_start = total_sockets;
	int ext_nodes_num = total_nodes - total_sockets;

	NodeInfo **nodeinfo =
		(NodeInfo **)malloc(sizeof(NodeInfo *) * total_sockets);
	if (nodeinfo == NULL) {
		tierd_error("Fail to allocate nodeinfo buffer: %s\n", strerror(errno));
		return -1;
	}
	for (int nid = 0; nid < total_sockets; nid++) {
		nodeinfo[nid] = (NodeInfo *)malloc(sizeof(NodeInfo) * total_nodes);
		if (nodeinfo[nid] == NULL) {
			tierd_error("Fail to allocate nodeinfo buffer: %s\n",
					strerror(errno));
			ret = -1;
			goto free;
		}
	}

	for (int from = 0; from < total_sockets; from++) {
		for (int to = 0; to < total_nodes; to++) {
			nodeinfo[from][to].val = numa_distance(from, to);
			nodeinfo[from][to].nid = to;
		}
		char *base = (char *)nodeinfo[from];
		qsort(base, dram_nodes_num, sizeof(NodeInfo), compare);
		qsort(base + ext_nodes_start, ext_nodes_num, sizeof(NodeInfo), compare);
	}

	for (int i = 0; i < total_sockets; i++)
		if (write_to_order_file(nodeinfo[i], i,
					0, total_nodes, FALLBACK_ORDER_TIER_FILE))
			ret = -1;

free:
	for (int nid = 0; nid < total_sockets; nid++)
		if (nodeinfo[nid])
			free(nodeinfo[nid]);
	if (nodeinfo)
		free(nodeinfo);

	return ret;
}

static inline void set_cluster(Cluster *cluster, int idx, int nid)
{
	cluster[idx].nodes[cluster[idx].num_nodes].nid = nid;
	cluster[idx].nodes[cluster[idx].num_nodes].val = -1;
	cluster[idx].num_nodes++;
}

static int node_clustering(NodeInfo *sorted, NodeInfo **nodeinfo)
{
	int ret = 0;
	int max_cluster_idx = 0;
	int base = total_nodes - 1;

	Cluster *cluster = (Cluster *)calloc(total_nodes, sizeof(Cluster));
	if (cluster == NULL) {
		tierd_error("Fail to allocate cluster buffer: %s\n", strerror(errno));
		return -1;
	}
	for (int nid = 0; nid < total_nodes; nid++) {
		cluster[nid].nodes = (NodeInfo *)calloc(total_nodes, sizeof(NodeInfo));
		if (cluster[nid].nodes == NULL) {
			tierd_error("Fail to allocate cluster buffer: %s\n", strerror(errno));
			ret = -1;
			goto free;
		}
	}

	set_cluster(cluster, max_cluster_idx, sorted[base].nid);
	for (int i = total_nodes - 2; i >= 0; i--) {
		if (sorted[base].val * (100.0f - TOLERANCE) > sorted[i].val * 100.0f) {
			base = i;
			max_cluster_idx++;
		}
		set_cluster(cluster, max_cluster_idx, sorted[i].nid);
	}
	max_cluster_idx++;

	for (int sid = 0, cnt = 0; sid < total_sockets; sid++, cnt = 0) {
		for (int cid = 0; cid < max_cluster_idx; cid++) {
			int size = cluster[cid].num_nodes;
			for (int i = 0; i < size; i++) {
				cluster[cid].nodes[i].val =
					numa_distance(sid, cluster[cid].nodes[i].nid);
			}

			qsort(cluster[cid].nodes, size, sizeof(NodeInfo), compare);

			for (int i = 0; i < size; i++) {
				nodeinfo[sid][cnt].nid = cluster[cid].nodes[i].nid;
				nodeinfo[sid][cnt++].val = cluster[cid].nodes[i].val;
			}
		}
	}

free:
	if (cluster) {
		for (int i = 0; i < total_nodes; i++)
			if (cluster[i].nodes)
				free(cluster[i].nodes);
		free(cluster);
	}

	return ret;
}

static int update_nodes_fallback_by_bandwidth()
{
	int ret = 0;
	NodeInfo *sorted = (NodeInfo *)malloc(sizeof(NodeInfo) * total_nodes);
	if (sorted == NULL) {
		tierd_error("Fail to allocate nodeinfo buffer: %s\n", strerror(errno));
		return -1;
	}
	for (int nid = 0; nid < total_nodes; nid++) {
		sorted[nid].nid = nid;
		sorted[nid].val = (int)max_bw[nid][BW_RDWR];
	}
	qsort(sorted, total_nodes, sizeof(NodeInfo), compare);

	NodeInfo **nodeinfo = (NodeInfo **)calloc(total_sockets, sizeof(NodeInfo *));
	if (nodeinfo == NULL) {
		tierd_error("Fail to allocate nodeinfo buffer: %s\n", strerror(errno));
		ret = -1;
		goto free;
	}
	for (int i = 0; i < total_sockets; i++) {
		nodeinfo[i] = (NodeInfo *)calloc(total_nodes, sizeof(NodeInfo));
		if (nodeinfo[i] == NULL) {
			tierd_error("Fail to allocate nodeinfo buffer: %s\n", strerror(errno));
			ret = -1;
			goto free;
		}
	}

	if (node_clustering(sorted, nodeinfo)) {
		tierd_error("Fail to node clustering: %s\n", strerror(errno));
		ret = -1;
		goto free;
	}

	for (int i = 0; i < total_sockets; i++)
		if (write_to_order_file(nodeinfo[i], i,
					0, total_nodes, FALLBACK_ORDER_BW_FILE))
			ret = -1;

free:
	if (nodeinfo) {
		for (int i = 0; i < total_sockets; i++)
			if (nodeinfo[i])
				free(nodeinfo[i]);
		free(nodeinfo);
	}

	if (sorted)
		free(sorted);

	return ret;
}

int update_fallback_list()
{
	if (update_nodes_fallback_by_tier())
		return -1;

	if (update_nodes_fallback_by_bandwidth())
		return -1;

	return 0;
}
