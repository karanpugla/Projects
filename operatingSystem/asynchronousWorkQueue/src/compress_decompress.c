#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/fs.h>
#include <linux/file.h>
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
#include "common_kernel.h"
#include "../common.h"


struct compressChunkHdr_t{
	char algo[10];
	loff_t length;
	loff_t origLength;
	loff_t nxtChunkOffset;
};

int compress_decompress_file ( char inp_name[], char oup_name[], char alg[], int decomp, int delInp){

	int err = 0, len, i, ret, pcomp = 0, obufferSize = 2*PAGE_SIZE;
	char *ibuffer = NULL, *obuffer = NULL;
	mm_segment_t old_fs;
	struct file *inputf=NULL, *outputf=NULL;
	struct crypto_pcomp	*ptfm = NULL;
	struct crypto_comp	*tfm = NULL;
	struct comp_request req;
	struct compressChunkHdr_t chunkHdr; 
	struct dentry *dir_dentry;
	loff_t ipos = 0, opos = 0, inputfSize, totalReads, currOffset;

	printk("compression/decompression: ifile=%s, ofile=%s\n", inp_name, oup_name);
	
	if(!inp_name || !oup_name){
		printk("NULL arguments.\n");
		err = -EINVAL;
		goto err;
	}
	inputf = filp_open(inp_name, O_RDONLY, 0); 
    if (IS_ERR(inputf)) {
		printk("cannot open input file\n");
        err = PTR_ERR(inputf);
		inputf = NULL;
        goto err;
    }
	inputfSize = inputf->f_path.dentry->d_inode->i_size;
	if(inputfSize == 0){
		printk("empty input file\n ");
		goto err;
	}
	outputf=filp_open(oup_name, O_CREAT|O_RDWR, inputf->f_path.dentry->d_inode->i_mode);
    if (IS_ERR(outputf)) {
		printk("cannot open output file\n");
        err=PTR_ERR(outputf);
		outputf = NULL;
        goto err;
    }
	if (decomp){
		printk("Decompression mode.\n");
		old_fs = get_fs();
		set_fs(get_ds());
		len = vfs_read(inputf, (void *)&chunkHdr, sizeof(struct compressChunkHdr_t), &ipos);
 		set_fs(old_fs);
		if (len < 0){
			printk("input file read error");
			err = len;
			goto err;
		}
		strcpy(alg,chunkHdr.algo);
		currOffset = 0;
		chunkHdr.length = ntohl(chunkHdr.length);
		chunkHdr.nxtChunkOffset = ntohl(chunkHdr.nxtChunkOffset);
		printk("Decompression algo is %s\n", alg);
		ipos = 0;
	}
	if(strcmp(alg, "lz4") == 0 || strcmp(alg, "lzo") == 0
		|| strcmp(alg, "zlib") == 0 || strcmp(alg, "deflate") == 0){
		if(strcmp(alg, "zlib") == 0){
			pcomp = 1;
			ptfm = crypto_alloc_pcomp(alg, 0, 0);
			if (IS_ERR(ptfm)){
				printk("Cannot allocate pcomp");
				ptfm = NULL;
				err = -ENOMEM;
				goto err;
			}
			if ( crypto_compress_init(ptfm) < 0){
				printk("(de)compress init error.\n");
				err = -EAGAIN;
				goto err;
			}
		}
		else {
			tfm = crypto_alloc_comp(alg, 0, 0);
			if (IS_ERR(tfm)){
				printk("Cannot allocate comp");
				tfm = NULL;
				err = -ENOMEM;
				goto err;
			}
		}

	}
	else{
		printk("Unsupported (de)compression algo.\n");
		err = -EINVAL;
		goto err;
	}
	ibuffer = kmalloc(2*PAGE_SIZE, GFP_KERNEL);
	obuffer = kmalloc(2*PAGE_SIZE, GFP_KERNEL);
	if (!ibuffer || !obuffer){
		printk("cannot allocate buffer.\n");
		err = -ENOMEM;
		goto err;
	}

	if(!decomp){
	//compression
		totalReads = inputfSize/PAGE_SIZE;
		if(inputfSize % PAGE_SIZE){
			totalReads += 1;
		}
		printk("totalReads=%lld\n", (long long)totalReads);
		strcpy(chunkHdr.algo, alg);
		for(i=1;i <= totalReads - 1;i++){
			obufferSize = 2*PAGE_SIZE;
			old_fs = get_fs();
			set_fs(get_ds());
			len = vfs_read(inputf, ibuffer, PAGE_SIZE, &ipos);
	 		set_fs(old_fs);
			if (len < 0){
				printk("input file read error");
				err = len;
				goto err;
			}
			else {
				if (pcomp){
				req.next_in = ibuffer;
				req.next_out = obuffer;
				req.avail_in = PAGE_SIZE;
				req.avail_out = 2*PAGE_SIZE;
				ret = crypto_compress_update(ptfm, &req);
				}
				else{
					ret = crypto_comp_compress(tfm, ibuffer, PAGE_SIZE, obuffer, &obufferSize);
				}
				if(ret >= 0){
					chunkHdr.length = htonl(obufferSize);
					chunkHdr.nxtChunkOffset = htonl(opos + obufferSize + sizeof(struct compressChunkHdr_t));
					chunkHdr.origLength = htonl(PAGE_SIZE);
				//	printk("writing compressed chunk: lenght=%lld, origLength=%lld, nxtOff=%lld\n", 
				//	(long long) ntohl(chunkHdr.length), (long long)ntohl(chunkHdr.origLength),
				//	 (long long)ntohl(chunkHdr.nxtChunkOffset));
					if (__kernel_write(outputf, (void *)&chunkHdr, sizeof(struct compressChunkHdr_t), &opos)
										!= sizeof(struct compressChunkHdr_t)) {	
	         			err = -EIO;
	          			goto err;
	       			}
					if (__kernel_write(outputf, obuffer, obufferSize, &opos) != obufferSize) {	
	         			err = -EIO;
	          			goto err;
	       			}
				}
				else {
					printk("compress ret error\n");
				}
			}
		}
		// last chunk
		obufferSize = 2*PAGE_SIZE;
		old_fs = get_fs();
		set_fs(get_ds());
		len = vfs_read(inputf, ibuffer, PAGE_SIZE, &ipos);
	 	set_fs(old_fs);
		if (len < 0){
			printk("input file read error");
			err = len;
			goto err;
		}
		else {
			if (pcomp){
				req.next_in = ibuffer;
				req.next_out = obuffer;
				req.avail_in = len;
				req.avail_out = 2*PAGE_SIZE;
				ret = crypto_compress_final(ptfm, &req) ;
			}
			else{
				printk("about to last compress\n");
				ret = crypto_comp_compress(tfm, ibuffer, len, obuffer, &obufferSize);
				printk("last compress ret %d\n", ret);
			}
	
			if(ret >= 0){
				chunkHdr.length = htonl(obufferSize);
				chunkHdr.nxtChunkOffset = 0xffffffff;
				chunkHdr.origLength = htonl(len);
			//	printk("writing compressed chunk: lenght=%lld, origLength=%lld, nxtOff=%lld\n", 
			//		(long long) ntohl(chunkHdr.length), (long long)ntohl(chunkHdr.origLength),
			//	 	(long long)ntohl(chunkHdr.nxtChunkOffset));
				if (__kernel_write(outputf, (void *)&chunkHdr, sizeof(struct compressChunkHdr_t), &opos)
									!= sizeof(struct compressChunkHdr_t)) {	
	         		err = -EIO;
	          		goto err;
	       		}
				if (__kernel_write(outputf, obuffer, obufferSize, &opos) != obufferSize) {	
					err = -EIO;
					goto err;
				}
			}
			else {
				printk("final compress ret error\n");
			}
			err = outputf->f_path.dentry->d_inode->i_size;
		}
		if (delInp > 0) {
			printk("Deletion of Input file requested\n");
			dir_dentry = lock_parent(inputf->f_path.dentry);
			if (vfs_unlink(dir_dentry->d_inode, inputf->f_path.dentry, NULL)){
				printk("cannot delete input file.\n");
			}
			unlock_dir(dir_dentry);
		}
	
	}
	else{
	// decompression
		do{
			// read header
			ipos = currOffset;
			old_fs = get_fs();
			set_fs(get_ds());
			len = vfs_read(inputf, (void *)&chunkHdr, sizeof(struct compressChunkHdr_t), &ipos);
	 		set_fs(old_fs);
			if (len < 0){
				printk("input file read error\n");
				err = len;
				goto err;
			}
			if(len < sizeof(struct compressChunkHdr_t)){
				printk("Cannot read chunkHdr\n");
				err = -EAGAIN;
				goto err;
			}
			chunkHdr.length = ntohl(chunkHdr.length);
			chunkHdr.nxtChunkOffset = ntohl(chunkHdr.nxtChunkOffset);
			chunkHdr.origLength = ntohl(chunkHdr.origLength);
			obufferSize = 2 * PAGE_SIZE;
			/*printk("Decompress: curroffset=%lld, chunklength=%lld, nxtoff=%lld, origlength=%lld,counter=%d\n",
			(long long)currOffset, (long long)chunkHdr.length, (long long)chunkHdr.nxtChunkOffset,
			(long long)chunkHdr.origLength, counter);*/
			ipos = currOffset + sizeof(struct compressChunkHdr_t);
			// read compressed data
			old_fs = get_fs();
			set_fs(get_ds());
			len = vfs_read(inputf, ibuffer, chunkHdr.length, &ipos);
	 		set_fs(old_fs);
			if (len < 0){
				printk("input file read error\n");
				err = len;
				goto err;
			}
			if(len < chunkHdr.length){
				printk("Cannot read complete\n");
				err = -EAGAIN;
				goto err;
			}
			//printk("Decompress: read len=%d bytes\n", len);
			ret = crypto_comp_decompress(tfm, ibuffer, chunkHdr.length, obuffer, &obufferSize);
			if(ret >= 0){
				if (__kernel_write(outputf, obuffer, obufferSize, &opos) != obufferSize) {	
					err = -EIO;
					goto err;
				}
			}
			else {
				printk("decompress ret error\n");
			}
			// prepare to decompress next chunk
			currOffset = chunkHdr.nxtChunkOffset;
			//printk("sizeof(unsigned long)=%d, sizeof(loff_t)=%d, currOffset=%lld\n", sizeof(unsigned long),
			//	sizeof(loff_t), (long long)currOffset);

		}while(currOffset != 0xffffffff);
		err = outputf->f_path.dentry->d_inode->i_size;
		if (delInp > 0) {
			printk("Deletion of Input file requested\n");
			dir_dentry = lock_parent(inputf->f_path.dentry);
			if (vfs_unlink(dir_dentry->d_inode, inputf->f_path.dentry, NULL)){
				printk("cannot delete input file.\n");
			}
			unlock_dir(dir_dentry);
		}
	
	}
	
err:
	if(inputf)
		filp_close(inputf, NULL);
	if(outputf)
		filp_close(outputf, NULL);
	if(tfm)
		crypto_free_comp(tfm);
	if(ptfm)
		crypto_free_pcomp(ptfm);
	if(ibuffer)
		kfree(ibuffer);
	if(obuffer)
		kfree(obuffer);
	return err;
}

