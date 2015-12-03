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
#include <crypto/hash.h>
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

/*Calculate checksum of a file
* returns 0 if successful
*/
int checksum_file( char inp_name[], char checksum[])
{
	struct file *inp_filp;
	int ret = 0, rb, i;
	loff_t read_pos = 0;

	char *out;
	char temp[3];
	char *buffer;
	struct shash_desc *desc;
	struct crypto_shash *tfm = NULL;

	printk("Calculating Checksum\n");

	inp_filp = filp_open(inp_name, O_RDONLY, 0);
	if (!inp_filp || IS_ERR(inp_filp)){
		ret = (int)PTR_ERR(inp_filp);
		return -ENOENT;
	}
	
	buffer = kmalloc(16, GFP_KERNEL);
	out = kmalloc(16, GFP_KERNEL);

	//setting md5 as checksum algo
	tfm = crypto_alloc_shash( "md5", 0, 0);
	if (IS_ERR(tfm)){
		printk(KERN_ERR "Failed to calculate md5 of given key :%ld\n", PTR_ERR(tfm));
		ret = (int)PTR_ERR(tfm);
		goto err;
	}
	desc =  kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	desc->tfm = tfm;
	desc->flags = 0;

	if(crypto_shash_init(desc) != 0){
		printk("shash init failed\n");
		goto err;
	}

	while ((rb = read_from_file(inp_filp, buffer, 16, &read_pos)) == 16){
		ret = crypto_shash_update(desc, buffer, 16);
	}
	if (rb != 0)
		ret = crypto_shash_update(desc, buffer, rb);
	
	ret = crypto_shash_final(desc, out);

	for( i = 0 ; i < 16; i++){
		sprintf(temp, "%02x", out[i] & 0xff);
		strncpy( &checksum[i*2], &temp[0], 2);
	}
	checksum[32] = '\0';
	printk("\n");
	printk("checksum is %s\n", checksum);
	printk("Checksum calculated.\n");
	return 16;
err:
	kfree(buffer);
	kfree(out);
	filp_close(inp_filp, NULL);
	if (tfm)
		crypto_free_shash(tfm);
	return ret;

}
