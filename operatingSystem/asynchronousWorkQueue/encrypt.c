#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/errno.h>

#define PAGESIZE 4096

int read_from_file( struct file *filp, char *buf, size_t len, loff_t *pos )
{
	int bytes;
	if(!filp->f_op->read)
		return -2;

	bytes = vfs_read(filp, buf, len, pos);
	return bytes;
}

int write_to_file(struct file *filp, char *buf, size_t len, loff_t *pos  )
{
	int bytes;
	if(!filp->f_op->write)
		return -2;

	bytes = vfs_write(filp, buf, len, pos);
	return bytes;

}


/*Encrypts given inp_name file to oup_name file
*/
int encrypt_file ( char inp_name[], char oup_name[], char key[])
{

	int ret = 0;
	
	struct file *inp_filp;
	struct dentry *inp_dentry;
	struct inode *inp_inode;
	int inp_file_size;
	int last_page_size;
	int pad_len;
	char pad_len_str[16];
	struct file *oup_filp;
	
	struct crypto_blkcipher *tfm = NULL;
	struct blkcipher_desc desc;
	struct scatterlist sctr;
	
	struct scatterlist sctr_key;
	struct hash_desc desc_key;
	struct crypto_hash *tfm_key;
	char md5key[16];
	char buf32[32];
	char iv_str[16];

	char *buffer;

	loff_t write_pos = 0;
	loff_t read_pos = 0;
	int wb, rb; //written bytes and read bytes
	mm_segment_t oldfs;

	if (inp_name == NULL || oup_name == NULL){
		ret = EINVAL;
		goto err1;
	}	
	
	inp_filp = filp_open(inp_name, O_RDONLY, 0);
	if (!inp_filp || IS_ERR(inp_filp)){
		ret = (int)PTR_ERR(inp_filp);
		goto err1;
	}
	
	oup_filp = filp_open(oup_name, O_RDONLY, 0);
	if (!oup_filp || IS_ERR(oup_filp)){
		ret = (int)PTR_ERR(oup_filp);
		goto err2;
	}

	inp_dentry = inp_filp->f_path.dentry;
	inp_inode = inp_dentry->d_inode;
	inp_file_size = inp_inode->i_size;

	//finding md5 of key
	sg_init_one(&sctr_key, key, 16);
	tfm_key = crypto_alloc_hash( "md5", 0, 0);
	if (IS_ERR(tfm_key)){
		printk(KERN_ERR "Failed to calculate md5 of given key :%ld\n", PTR_ERR(tfm_key));
		ret = (int)PTR_ERR(tfm_key);
		goto err3;
	}

	desc_key.tfm = tfm_key;
	desc_key.flags = 0;
	
	if (crypto_hash_digest( &desc_key, &sctr_key, 2, md5key)){
		ret = -EINVAL;
		goto err3;
	}
	
	//initializing encryption objects
	tfm = crypto_alloc_blkcipher("cbc(aes)", 0, 0);
	if (IS_ERR(tfm)){
		printk(KERN_ERR "Failed to load tranform for encryption :%ld\n", PTR_ERR(tfm));
		ret = (int)PTR_ERR(tfm);
		goto err3;
	}

	if ( (ret = crypto_blkcipher_setkey(tfm, key, sizeof(key))) < 0){
		printk(KERN_ERR "Failed to set AES key: %d\n", ret);
		goto err3;
	}
	
	desc.tfm = tfm;
	desc.flags = 0;

	//starting encryption
	buffer = (char*) kmalloc(PAGESIZE, GFP_KERNEL);
	memset(buf32, '\0', 32),

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	if ( (wb = write_to_file(oup_filp, buf32, 32, &write_pos)) < 0){
		printk("Unable to write zero header to output file\n");
		ret = wb;
		goto err4;
	}
	strncpy(iv_str, "thisisa16charstr", 16);
	crypto_blkcipher_set_iv(tfm, iv_str, 16);

	while ((rb = read_from_file(inp_filp, buffer, PAGESIZE, &read_pos)) == PAGESIZE){
		sg_init_one(&sctr, buffer, PAGE_SIZE);
		ret = crypto_blkcipher_encrypt(&desc, &sctr, &sctr, PAGESIZE);
		ret = sg_copy_to_buffer(&sctr, PAGESIZE, buffer, PAGESIZE);
		if ( (wb = write_to_file(oup_filp, buffer, PAGESIZE, &write_pos)) < 0){
			ret = wb;
			printk("Error in writing encrypted data to list\n");
			goto err4;
		}
	}

	if (rb != 0){
		last_page_size = inp_file_size - (inp_file_size/PAGESIZE)*PAGESIZE;
		pad_len = ((last_page_size/16)+1)*16 - rb;
		memset(buffer+rb, '~', last_page_size-rb-1);
		last_page_size += pad_len;
		sg_init_one(&sctr, buffer, last_page_size);
		ret = crypto_blkcipher_encrypt(&desc, &sctr, &sctr, last_page_size);
		ret = sg_copy_to_buffer(&sctr, last_page_size, buffer, last_page_size);
		if ((wb = write_to_file(oup_filp, buffer, last_page_size, &write_pos)) < 0){
			ret = wb;
			goto err4;
		}
	}

	strncpy(buf32, md5key, 16);
	sprintf(pad_len_str, "%d", inp_file_size);
	strncpy(buf32+16, pad_len_str, 16);
	write_pos = 0;
	if ((wb = write_to_file(oup_filp, buf32, 32, &write_pos)) < 0){
		ret = wb;
		printk("Error in writing final header to encrypted file\n");
		goto err4;
	}
	set_fs(oldfs);
	return 1;

err4:
	set_fs(oldfs);
	kfree(buffer);
err3:
	filp_close(oup_filp, NULL);
err2:
	filp_close(inp_filp, NULL);
	
err1:
	return ret;
}


/*Decrypts a file with inp_name to oup_name
*/
int decrypt_file ( char inp_name[], char oup_name[], char key[])
{
	int ret = 0;
	
	struct file *inp_filp;
	struct dentry *inp_dentry;
	struct inode *inp_inode;
	int inp_file_size;
	int last_page_size;
	char pad_len_str[16];
	int pad_len;
	int nocptw, ebflp, i = 0;

	struct file *oup_filp;
	
	struct crypto_blkcipher *tfm = NULL;
	struct blkcipher_desc desc;
	struct scatterlist sctr;

	struct scatterlist sctr_key;
	struct hash_desc desc_key;
	struct crypto_hash *tfm_key;
	char md5key[16];
	char iv_str[16];
	char buf32[32];

	char *buffer;

	loff_t write_pos = 0;
	loff_t read_pos = 0;
	int wb, rb; //written bytes and read bytes
	mm_segment_t oldfs;

	if (inp_name == NULL || oup_name == NULL){
		ret = EINVAL;
		goto err1;
	}	
	
	inp_filp = filp_open(inp_name, O_RDONLY, 0);
	if (!inp_filp || IS_ERR(inp_filp)){
		ret = (int)PTR_ERR(inp_filp);
		goto err1;
	}
	
	oup_filp = filp_open(oup_name, O_RDONLY, 0);
	if (!oup_filp || IS_ERR(oup_filp)){
		ret = (int)PTR_ERR(oup_filp);
		goto err2;
	}

	inp_dentry = inp_filp->f_path.dentry;
	inp_inode = inp_dentry->d_inode;
	inp_file_size = inp_inode->i_size;

	//finding md5 of key
	sg_init_one(&sctr_key, key, 16);
	tfm_key = crypto_alloc_hash( "md5", 0, 0);
	if (IS_ERR(tfm_key)){
		printk(KERN_ERR "Failed to calculate md5 of given key :%ld\n", PTR_ERR(tfm_key));
		ret = (int)PTR_ERR(tfm_key);
		goto err3;
	}

	desc_key.tfm = tfm_key;
	desc_key.flags = 0;
	
	if (crypto_hash_digest( &desc_key, &sctr_key, 2, md5key)){
		ret = -EINVAL;
		goto err3;
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	rb = read_from_file(inp_filp, buf32, 32, &read_pos);
	set_fs(oldfs);

	if (strncmp(buf32, md5key, 32) != 0){
		printk("Decryption key provided is incorrect\n");
		ret = -EACCES;
		goto err3;
	}

	strncpy(pad_len_str, buf32+16, 16);
	pad_len_str[15] = '\0';
	pad_len = kstrtoint(pad_len_str, 10, &inp_file_size);

	tfm = crypto_alloc_blkcipher( "cbc(aes)", 0, 0);
	if (IS_ERR(tfm)){
		printk(KERN_ERR "Failed to load transform for decrypt\n");
		ret = -EINVAL;
		goto err3;
	}

	if ( (ret = crypto_blkcipher_setkey(tfm, key, sizeof(key))) < 0){
		printk(KERN_ERR "Failed to set AES key: %d\n", ret);
		goto err3;
	}

	desc.tfm = tfm;
	desc.flags = 0;

	buffer = (char*)kmalloc(PAGESIZE, GFP_KERNEL);
	memset(buf32, '\0', 32),
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	
	nocptw = inp_file_size/PAGESIZE;
	strncpy(iv_str, "thisisa16charstr", 16);
	crypto_blkcipher_set_iv(tfm, iv_str, 16);

	i = 0;
	while ( i < nocptw){
		rb = read_from_file(inp_filp, buffer, PAGESIZE, &read_pos);
		sg_init_one(&sctr, buffer, PAGESIZE);
		ret = crypto_blkcipher_decrypt(&desc, &sctr, &sctr, PAGESIZE);
		ret = sg_copy_to_buffer(&sctr, PAGESIZE, buffer, PAGESIZE);
		if( (wb = write_to_file(oup_filp, buffer, PAGESIZE, &write_pos)) < 0){
			ret = wb;
			goto err4;
		}
		i++;
	}

	ebflp = inp_file_size - (inp_file_size/PAGESIZE)*PAGESIZE;
	last_page_size = ((ebflp/16)+1)*16;
	if((rb = read_from_file(inp_filp, buffer, last_page_size, &read_pos)) < 0){
		ret = -EINVAL;
		goto err4;
	}
	sg_init_one(&sctr, buffer, PAGESIZE);
	ret = crypto_blkcipher_decrypt(&desc, &sctr, &sctr, last_page_size);
	ret = sg_copy_to_buffer(&sctr, PAGESIZE, buffer, last_page_size);

	if ((wb = write_to_file(oup_filp, buffer, ebflp, &write_pos)) < 0){
		ret = wb;
		goto err4;
	}
	return 1;

err4:
	set_fs(oldfs);
	kfree(buffer);
err3:
	filp_close(oup_filp, NULL);
err2:
	filp_close(inp_filp, NULL);
err1:
	return ret;
}












