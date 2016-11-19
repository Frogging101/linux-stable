#ifndef __BTRFS_MRSATURN_
#define __BTRFS_MRSATURN_

#include <linux/completion.h>

extern struct completion mismatch_processed;
extern bool mismatch_fixed;

int btrfs_netlink_init(void);
int btrfs_csmm_sendmismatch(dev_t dev, u64 phys, size_t len, u32 csum_actual,
                            u32 csum_expected);
bool btrfs_mrsaturn_available(void);

#endif
