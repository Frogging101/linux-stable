#include "net/netlink.h"
#include "net/genetlink.h"
#include "linux/string.h"
#include "ctree.h"
#include "mrsaturn.h"

struct completion mismatch_processed;
bool mismatch_fixed = false;

static int dpid = 0;
static struct net *netns = NULL;
static u32 seq;

enum {
    BTRFS_CSMM_C_UNSPEC,
    BTRFS_CSMM_C_HELLO,
    BTRFS_CSMM_C_GOODBYE,
    BTRFS_CSMM_C_ECHO,
    BTRFS_CSMM_C_MISMATCH,
    BTRFS_CSMM_C_MISMATCH_PROCESSED,
    __BTRFS_CSMM_C_MAX,
};

enum {
    BTRFS_CSMM_A_UNSPEC,
    BTRFS_CSMM_A_MSG,
    BTRFS_CSMM_A_CSUM_ACTUAL,
    BTRFS_CSMM_A_CSUM_EXPECTED,
    BTRFS_CSMM_A_PHYS,
    BTRFS_CSMM_A_LENGTH,
    BTRFS_CSMM_A_MAJOR,
    BTRFS_CSMM_A_MINOR,
    BTRFS_CSMM_A_FIXED,
    __BTRFS_CSMM_A_MAX
};

#define BTRFS_CSMM_A_MAX (__BTRFS_CSMM_A_MAX - 1)
#define BTRFS_CSMM_C_MAX (__BTRFS_CSMM_C_MAX - 1)

static int btrfs_csmm_hello(struct sk_buff *skb, struct genl_info *info);
static int btrfs_csmm_goodbye(struct sk_buff *skb, struct genl_info *info);
static int btrfs_csmm_echo(struct sk_buff *skb, struct genl_info *info);
static int btrfs_csmm_mismatch_processed(struct sk_buff *skb,
                                         struct genl_info *info);

static struct nla_policy btrfs_csmm_genl_policy[BTRFS_CSMM_A_MAX + 1] = {
    [BTRFS_CSMM_A_MSG] = { .type = NLA_NUL_STRING },
};

static struct genl_family btrfs_csmm_gnl_family = {
    .id = GENL_ID_GENERATE,
    .hdrsize = 0,
    .name = "BTRFS_CSMM",
    .version = 1,
    .maxattr = BTRFS_CSMM_A_MAX,
};

static struct genl_ops btrfs_csmm_gnl_ops[4] = {
    {
        .cmd = BTRFS_CSMM_C_ECHO,
        .flags = 0,
        .policy = btrfs_csmm_genl_policy,
        .doit = btrfs_csmm_echo,
        .dumpit = NULL,
    },
    {
        .cmd = BTRFS_CSMM_C_HELLO,
        .flags = GENL_ADMIN_PERM,
        .policy = NULL,
        .doit = btrfs_csmm_hello,
        .dumpit = NULL,
    },
    {
        .cmd = BTRFS_CSMM_C_GOODBYE,
        .flags = GENL_ADMIN_PERM,
        .policy = NULL,
        .doit = btrfs_csmm_goodbye,
        .dumpit = NULL,
    },
    {
        .cmd = BTRFS_CSMM_C_MISMATCH_PROCESSED,
        .flags = GENL_ADMIN_PERM,
        .policy = NULL,
        .doit = btrfs_csmm_mismatch_processed,
        .dumpit = NULL,
    },
};

bool btrfs_mrsaturn_available() {
    if(!dpid || !netns) {
        return false;
    }
    return true;
}

static int btrfs_csmm_hello(struct sk_buff *skb, struct genl_info *info) {
    printk(KERN_INFO "BTRFS: Registering helper port ID %d\n", info->snd_portid);
    if(dpid)
        printk(KERN_WARNING "Previous helper (ID %d) did not unregister; is it still running or did it crash?\n",
               dpid);
    dpid = info->snd_portid;
    netns = genl_info_net(info);
    return 0;
}
static int btrfs_csmm_goodbye(struct sk_buff *skb, struct genl_info *info) {
    printk(KERN_INFO "BTRFS: Unregistering helper port ID %d\n", dpid);
    dpid = 0;
    netns = NULL;
    return 0;
}

static int btrfs_csmm_echo(struct sk_buff *skb, struct genl_info *info) {
    struct nlattr *msgattr = info->attrs[BTRFS_CSMM_A_MSG];
    printk("BTRFS: netlink echo test: %s\n", (char *)nla_data(msgattr));
    if(strcmp((char *)nla_data(msgattr), "ping") == 0) {
        struct sk_buff *nskb;
        void *msg_head;
        nskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
        msg_head = genlmsg_put(nskb, info->snd_portid, info->snd_seq,
                               &btrfs_csmm_gnl_family, 0, BTRFS_CSMM_C_ECHO);
        nla_put_string(nskb, BTRFS_CSMM_A_MSG, "pong!");
        genlmsg_end(nskb, msg_head);
        genlmsg_reply(nskb, info);
    }
    return 0;
}

static int btrfs_csmm_mismatch_processed(struct sk_buff *skb,
                                         struct genl_info *info) {
    struct nlattr *fixed = info->attrs[BTRFS_CSMM_A_FIXED];
    mismatch_fixed = nla_get_flag(fixed);
    complete(&mismatch_processed);
    return 0;
}

int btrfs_csmm_sendmismatch(dev_t dev, u64 phys, size_t len, u32 csum_actual,
                            u32 csum_expected) {
    int err;
    unsigned int maj = MAJOR(dev);
    unsigned int min = MINOR(dev);
    struct sk_buff *skb;
    void *msg_head;

    if(!dpid || !netns)
        return -ECONNREFUSED;

    skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    msg_head = genlmsg_put(skb, dpid, seq++, &btrfs_csmm_gnl_family,
                           NLM_F_REQUEST, BTRFS_CSMM_C_MISMATCH);
    nla_put_u32(skb, BTRFS_CSMM_A_CSUM_ACTUAL, csum_actual);
    nla_put_u32(skb, BTRFS_CSMM_A_CSUM_EXPECTED, csum_expected);
    nla_put_u64(skb, BTRFS_CSMM_A_PHYS, phys);
    nla_put_u64(skb, BTRFS_CSMM_A_LENGTH, len);
    nla_put_u32(skb, BTRFS_CSMM_A_MAJOR, maj);
    nla_put_u32(skb, BTRFS_CSMM_A_MINOR, min);

    genlmsg_end(skb, msg_head);
    err = genlmsg_unicast(netns, skb, dpid);

    return err;
}

int btrfs_netlink_init() {
    int err = genl_register_family_with_ops(&btrfs_csmm_gnl_family, btrfs_csmm_gnl_ops);
    if(err) {
        printk(KERN_WARNING "BTRFS: Failed to register netlink family: %d\n", err);
        return err;
    }
    init_completion(&mismatch_processed);
    printk(KERN_INFO "BTRFS: Registered netlink family\n");
    return 0;
}
