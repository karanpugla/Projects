#include "common_kernel.h"
int write_to_file(struct file *filp, char *buf, size_t len, loff_t *pos)
{
	int bytes;
	mm_segment_t oldfs;
	if(!filp->f_op->write)
		return -2;

	oldfs = get_fs();
	set_fs(KERNEL_DS);	
	bytes = vfs_write(filp, buf, len, pos);
	set_fs(oldfs);
	return bytes;

}
int read_from_file( struct file *filp, char *buf, size_t len, loff_t *pos )
{
	int bytes;
	mm_segment_t oldfs;
	if(!filp->f_op->read)
		return -2;

	oldfs = get_fs();
	set_fs(KERNEL_DS);	
	bytes = vfs_read(filp, buf, len, pos);
	set_fs(oldfs);
	return bytes;
}
struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir;

	dir = dget_parent(dentry);
	mutex_lock_nested(&(dir->d_inode->i_mutex), I_MUTEX_PARENT);
	return dir;
}

void unlock_dir(struct dentry *dir)
{
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
}

