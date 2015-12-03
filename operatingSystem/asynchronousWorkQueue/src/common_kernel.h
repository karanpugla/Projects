#ifndef _SYS_SUBMIT_JOB_K_H_
#define _SYS_SUBMIT_JOB_K_H_
#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <crypto/compress.h>
#include <linux/scatterlist.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>
#include <linux/fs.h>

int write_to_file(struct file *filp, char *buf, size_t len, loff_t *pos);
int read_from_file(struct file *filp, char *buf, size_t len, loff_t *pos);
struct dentry *lock_parent(struct dentry *dentry);
void unlock_dir(struct dentry *dir);



#endif
