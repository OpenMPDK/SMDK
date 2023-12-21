#ifndef _SMDK_CONTROL_H_
#define _SMDK_CONTROL_H_

int soft_interleaving_group_add(int argc, const char **argv, int target_node, int *count);
int soft_interleaving_group_remove(int argc, const char **argv, int target_node, int *count);
int soft_interleaving_group_node(int *count);
int soft_interleaving_group_noop(int *count);
int soft_interleaving_group_list_node(int target);
int soft_interleaving_group_list_dev(char *dev);

#endif /* _SMDK_CONTROL_H_ */
