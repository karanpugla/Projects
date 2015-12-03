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
#include "../common.h"
#include "common_kernel.h"

/*Encrypts given inp_name file to oup_name file
* returns 1 if successful
*/
int encrypt_file ( char inp_name[], char oup_name[], char keyinp[], int delInp)
{

	int ret = 0;
	
	struct file *inp_filp;
	struct dentry *inp_dentry;
	struct inode *inp_inode;
	struct dentry *dir_dentry;
	int inp_file_size;
	int last_page_size;
	int pad_len;
	char pad_len_str[16];
	char key[16];
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
	int i=0;

	loff_t write_pos = 0;
	loff_t read_pos = 0;
	int wb, rb; //written bytes and read bytes

	printk("Starting encryption\n");
	if (inp_name == NULL || oup_name == NULL){
		ret = EINVAL;
		goto err1;
	}	
	
	inp_filp = filp_open(inp_name, O_RDONLY, 0);
	if (!inp_filp || IS_ERR(inp_filp)){
		ret = (int)PTR_ERR(inp_filp);
		goto err1;
	}
	
	inp_dentry = inp_filp->f_path.dentry;
	inp_inode = inp_dentry->d_inode;
	inp_file_size = inp_inode->i_size;

	oup_filp = filp_open(oup_name, O_WRONLY|O_CREAT, inp_inode->i_mode);
	if (!oup_filp || IS_ERR(oup_filp)){
		ret = (int)PTR_ERR(oup_filp);
		goto err2;
	}
	
	strncpy(key, keyinp, 16);

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

	if ( (ret = crypto_blkcipher_setkey((void*)tfm, key, sizeof(key))) < 0){
		printk(KERN_ERR "Failed to set AES key: %d\n", ret);
		goto err3;
	}
	desc.tfm = tfm;
	desc.flags = 0;

	//starting encryption
	buffer = (char*) kmalloc(PAGE_SIZE, GFP_KERNEL);
	memset(buf32, '\0', 32);

	if ( (wb = write_to_file(oup_filp, buf32, 32, &write_pos)) < 0){
		printk("Unable to write zero header to output file\n");
		ret = wb;
		goto err4;
	}
	strncpy(iv_str, "thisisa16charstr", 16);
	crypto_blkcipher_set_iv(tfm, iv_str, 16);

	printk("Reading...\n");
	while ((rb = read_from_file(inp_filp, buffer, PAGE_SIZE, &read_pos)) == PAGE_SIZE){
		sg_init_one(&sctr, buffer, PAGE_SIZE);
		ret = crypto_blkcipher_encrypt(&desc, &sctr, &sctr, PAGE_SIZE);
		ret = sg_copy_to_buffer(&sctr, PAGE_SIZE, buffer, PAGE_SIZE);
		if ( (wb = write_to_file(oup_filp, buffer, PAGE_SIZE, &write_pos)) < 0){
			ret = wb;
			printk("Error in writing encrypted data to list\n");
			goto err4;
		}
	}
	if (rb != 0){
		last_page_size = inp_file_size - (inp_file_size/PAGE_SIZE)*PAGE_SIZE;
		pad_len = ((last_page_size/16)+1)*16 - rb;
		for(i = rb; i < last_page_size; i++)
			buffer[i] = 126;
		i = 0;
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
	printk("Encryption Over\n");
	if(delInp == 1){
		printk("Unlinking input file.\n");
		dir_dentry = lock_parent(inp_dentry);
		if ( vfs_unlink(dir_dentry->d_inode, inp_dentry, NULL)){
			printk("Error in unlinking input file while decryption\n");
		}
		printk("File Unlinked\n");
		unlock_dir(dir_dentry);
	}
	return 1;

err4:
	kfree(buffer);
err3:
	filp_close(oup_filp, NULL);
err2:
	filp_close(inp_filp, NULL);
	
err1:
	return ret;
}

/*Decrypts a file with inp_name to oup_name
* returns 1 if successful
*/

int decrypt_file ( char inp_name[], char oup_name[], char keyinp[], int delInp)
{
	int ret = 0;
	
	struct file *inp_filp;
	struct dentry *inp_dentry;
	struct inode *inp_inode;
	struct dentry *dir_dentry;
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
	char key[16];

	char *buffer;

	loff_t write_pos = 0;
	loff_t read_pos = 0;
	int wb, rb; //written bytes and read bytes

	printk("Decryption started\n");
	if (inp_name == NULL || oup_name == NULL){
		ret = EINVAL;
		goto err1;
	}	
	
	inp_filp = filp_open(inp_name, O_RDONLY, 0);
	if (!inp_filp || IS_ERR(inp_filp)){
		ret = (int)PTR_ERR(inp_filp);
		goto err1;
	}
	
	inp_dentry = inp_filp->f_path.dentry;
	inp_inode = inp_dentry->d_inode;
	inp_file_size = inp_inode->i_size;
	
	oup_filp = filp_open(oup_name, O_WRONLY|O_CREAT, inp_inode->i_mode);
	if (!oup_filp || IS_ERR(oup_filp)){
		ret = (int)PTR_ERR(oup_filp);
		goto err2;
	}
	
	strncpy(key, keyinp, 16);

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

	rb = read_from_file(inp_filp, buf32, 32, &read_pos);

	if (strncmp(buf32, md5key, 16) != 0){
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

	buffer = (char*)kmalloc(PAGE_SIZE, GFP_KERNEL);
	memset(buf32, '\0', 32),
	
	nocptw = inp_file_size/PAGE_SIZE;
	strncpy(iv_str, "thisisa16charstr", 16);
	crypto_blkcipher_set_iv(tfm, iv_str, 16);

	i = 0;
	while ( i < nocptw){
		rb = read_from_file(inp_filp, buffer, PAGE_SIZE, &read_pos);
		sg_init_one(&sctr, buffer, PAGE_SIZE);
		ret = crypto_blkcipher_decrypt(&desc, &sctr, &sctr, PAGE_SIZE);
		ret = sg_copy_to_buffer(&sctr, PAGE_SIZE, buffer, PAGE_SIZE);
		if( (wb = write_to_file(oup_filp, buffer, PAGE_SIZE, &write_pos)) < 0){
			ret = wb;
			goto err4;
		}
		i++;
	}

	ebflp = inp_file_size - (inp_file_size/PAGE_SIZE)*PAGE_SIZE;
	last_page_size = ((ebflp/16)+1)*16;
	if((rb = read_from_file(inp_filp, buffer, last_page_size, &read_pos)) < 0){
		ret = -EINVAL;
		goto err4;
	}
	sg_init_one(&sctr, buffer, PAGE_SIZE);
	ret = crypto_blkcipher_decrypt(&desc, &sctr, &sctr, last_page_size);
	ret = sg_copy_to_buffer(&sctr, PAGE_SIZE, buffer, last_page_size);

	if ((wb = write_to_file(oup_filp, buffer, ebflp, &write_pos)) < 0){
		ret = wb;
		goto err4;
	}
	printk("Decryption over.\n");
	if(delInp == 1){
		printk("Unlinking input file.\n");
		dir_dentry = lock_parent(inp_dentry);
		if ( vfs_unlink(dir_dentry->d_inode, inp_dentry, NULL)){
			printk("Error in unlinking input file while decryption\n");
		}
		printk("File Unlinked\n");
		unlock_dir(dir_dentry);
	}
	return 1;
err4:
	kfree(buffer);
err3:
	filp_close(oup_filp, NULL);
err2:
	filp_close(inp_filp, NULL);
err1:
	return ret;
}

