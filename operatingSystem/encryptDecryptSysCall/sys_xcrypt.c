#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/random.h>
#include <linux/byteorder/generic.h>
#include <uapi/linux/limits.h>
#include "./include/xcrypt_common.h"

const int ENC=1;
const int DNC=2;
const int AES=1;
const int BLOWFISH=2;

// Holds IV initialization
typedef struct
{
    uint64_t pageno;
    uint64_t ino;
}IV;

// Goes to encrypted file Header
typedef struct
{
    char md5key[16];
    char iv[16];
    char cipheralgo[16];
    int padding;
} xcrypt_preamble_t;

// Debug to convert bytes to HEX
void HEXprintk(const char*msg, void *scratchpad,int count);

// Crypto MD5 hash to hash/dehash passphrase
void domd5hash(char * key,char *hash);

asmlinkage extern long (*sysptr)(void *arg);

asmlinkage long xcrypt(void *arg)
{
    // Will be filled in kern space from user space
    char  *input=NULL,*output=NULL,*passphrase=NULL,*cipheralgo=NULL, *tempoutput=NULL ;
    int op; //ENCRYPT or DECRYPT
    // AES or BLOWFISH
    int cipheralgooption;
   // Holders to query files 
    struct path outputfpath, tempoutputfpath;
    // Used to take care during cleanup. Do not remove output file if it existed before start
    int outputfexisted=false; 
    struct file *inputf=NULL, *outputf=NULL, *tempoutputf=NULL;
    // Output file must have same permissions as input file.
    umode_t outputf_mode; 
    struct kstat inputf_stat, outputf_stat; 
    int err=0,len; 
    // Used in Input/Output read write loop
    loff_t ipos,opos;
    mm_segment_t old_fs;
    // For use with Crypto API
    struct crypto_blkcipher *blkcipher = NULL;
    struct scatterlist sg;
    struct blkcipher_desc desc;
    IV *iv=NULL ;
    char *buffer=NULL;
    int i=0;
    int padcount=0;
    xcrypt_preamble_t preamble;
    // MD5 hashed passphrase that will goto or come from preamble
    char keyhash[16];
    
    if (arg == NULL)
    {
		err= -EINVAL;
        goto out_last;
    }
        

    if( ! ( input= kmalloc(PATH_MAX,GFP_KERNEL)) ) 
    {
		err= -ENOMEM;
        goto out_last;
    }
    len=strncpy_from_user(input,((xcrypt_arg_t *)arg)->input, PATH_MAX);
    if( len<0 )
    {
        err=-EFAULT;
        goto out_argfree;
    }
    input[len]='\0';
    if( ! ( output= kmalloc(PATH_MAX,GFP_KERNEL)) ) 
    {
		err= -ENOMEM;
        goto out_argfree;
    }

    len=strncpy_from_user(output,((xcrypt_arg_t *)arg)->output, PATH_MAX);
    if( len<0 )
    {
        err=-EFAULT;
        goto out_argfree;
    }
    output[len]='\0';
    if( ! ( passphrase=kmalloc(16,GFP_KERNEL)) ) 
    {
		err= -ENOMEM;
        goto out_argfree;
    }
     
    len=strncpy_from_user(passphrase,((xcrypt_arg_t *)arg)->passphrase, 16);
    if(len < 0)
    {
        err= -EFAULT;
        goto out_argfree;
    }
    else if(len < 16 || len > 16)
    {
       err= -EINVAL;
       printk("Hashed passphrase length can only be 16 bytes %d\n",len);
       goto out_argfree;
    }
    passphrase[len]='\0';

    if( ! ( cipheralgo=kmalloc(16,GFP_KERNEL)) ) 
    {
		err= -ENOMEM;
        goto out_argfree;
    }
     
    len=strncpy_from_user(cipheralgo,((xcrypt_arg_t *)arg)->cipheralgo, 16);
    if(len < 0)
    {
        err= -EFAULT;
        goto out_argfree;
    }
    else if(strcmp(((xcrypt_arg_t *)arg)->cipheralgo,"cbc(aes)")==0 )
        cipheralgooption=AES;
    #ifdef EXTRA_CREDIT
    else if(strcmp(((xcrypt_arg_t *)arg)->cipheralgo,"cbc(blowfish)")==0 )
        cipheralgooption=BLOWFISH;
    #endif
    else
    {
       err= -EINVAL;
       goto out_argfree;
    }
    cipheralgo[len]='\0';


    op=((xcrypt_arg_t *)arg)->op;
    if(op!=ENCRYPT && op!=DECRYPT)
    {
        err= -EINVAL;
       printk("Operation can be ENCRYPT=%d or DECRYPT=%d\n",ENCRYPT,DECRYPT);
        goto out_argfree;
    }
    // The output behavior is just like 'cp' command.
    // Symlinks are followed by default for both input and output.
    // Output file will be created if it did not exist with same permissions as of input file.
    // If output file already existed, only the contents will be overwritten. 
    
    
    inputf=filp_open(input,O_RDONLY,0); 
    if (IS_ERR(inputf))
    {
        err=PTR_ERR(inputf);
        goto out_argfree;
    }
    
    if( (err=vfs_getattr(&inputf->f_path,&inputf_stat)) )
    {
        goto out_fclose;
    }
    
    if(S_ISDIR(inputf_stat.mode))
    {
        err=-EISDIR;
        goto out_fclose; 
    }

    outputf_mode=inputf_stat.mode;
    if( (kern_path(output,LOOKUP_FOLLOW, &outputfpath)) == 0  )
    {
        outputfexisted=true;
       if( (err=vfs_getattr(&outputfpath,&outputf_stat)) )
       {
           goto out_fclose;
       }
       if(S_ISDIR(outputf_stat.mode))
       {
            err=-EISDIR;
            goto out_fclose;
       }
       if( ! S_ISREG(outputf_stat.mode) )
       {
            err=-EINVAL;
            goto out_fclose;
       }
   // Input and output file must not be same

      if( inputf->f_inode->i_sb->s_dev == outputfpath.dentry->d_inode->i_sb->s_dev\
      && \
      inputf->f_inode->i_ino == outputfpath.dentry->d_inode->i_ino
      )
      {
        printk("I/O same\n");
          err= -EBUSY;
          goto out_fclose;
      }
    }

   

    if( ! ( tempoutput= kmalloc(PATH_MAX,GFP_KERNEL)) ) 
    {
		err= -ENOMEM;
        goto out_fclose;
    }

    strcpy ( tempoutput, output);
    strcat ( tempoutput, ".tmp");
    tempoutputf=filp_open(tempoutput,O_CREAT|O_RDWR,outputf_mode);
    if (IS_ERR(tempoutputf))
    {
        err=PTR_ERR(tempoutputf);
        goto out_fclose;
    }
    

    blkcipher = crypto_alloc_blkcipher(cipheralgo, 0, 0);
    if (IS_ERR(blkcipher)) {
        printk("could not allocate blkcipher handle for %s\n", cipheralgo);
        err= -EINVAL ;
        goto out_fclose;
    }
    
    desc.flags = 0;
    desc.tfm = blkcipher;

    if( ! ( buffer=kmalloc(PAGE_SIZE,GFP_KERNEL)) ) 
    {
		err= -ENOMEM;
        goto out_fclose;
    }
    
    if( ! ( iv=kmalloc(sizeof(IV),GFP_KERNEL)) ) 
    {
		err= -ENOMEM;
        goto out_bufferfree;
    }
   
   // OK, let's do it.
    
    sg_init_one(&sg, buffer, PAGE_SIZE);
    if(op == ENCRYPT)
    {
      ipos = inputf->f_pos;
      opos = tempoutputf->f_pos;
      
      if (crypto_blkcipher_setkey(blkcipher, passphrase, 16 )) {
          printk("key could not be set\n");
          err = -EAGAIN;
          goto out_bufferfree;
      }
       iv->pageno=(size_t)buffer >> PAGE_SHIFT;
       iv->ino=inputf_stat.ino;
       #ifdef EXTRA_CREDIT
       crypto_blkcipher_set_iv(blkcipher, (char *)iv, cipheralgooption==AES ? 16 : 8);  
       #else 
       crypto_blkcipher_set_iv(blkcipher, (char *)iv, 16);  
       #endif
       //printk("iv key length is %d",cipheralgooption==AES ? 16 : 8);
       //HEXprintk("key is: ",passphrase,16);
       //HEXprintk("IV is: ",iv,16);

     // Make space for Preamble. All output goes after this. It will be filled later.
     if (__kernel_write(tempoutputf, (void *)&preamble, sizeof(xcrypt_preamble_t), &opos) != sizeof(xcrypt_preamble_t))
     {
        err=-EIO;
        goto out_bufferfree;
     }

    // Input/Output loop
     do
     {  
       old_fs = get_fs();
       set_fs(get_ds());
       len = vfs_read(inputf, buffer, PAGE_SIZE, &ipos);
       set_fs(old_fs);
       //printk("read len is:%d ",len);
       //HEXprintk("buffer is: ",buffer,16);
       
       if (len < 0) 
       {
           err= -EIO;
           goto out_bufferfree;
       }
       // Needs padding
       else if(len>0 && len < PAGE_SIZE)
       {
          padcount=PAGE_SIZE-len;
       //   printk("padcount is:%d\n",padcount);
          for(i=0;i<padcount;i++)
              buffer[len+i]='$';
          //HEXprintk("Padded buffer is: ",buffer,16);
       }
       else if(len==0)
            break;
      /* encrypt data in place */
       if( crypto_blkcipher_encrypt(&desc, &sg, &sg, PAGE_SIZE) < 0) 
       {
          err=-EAGAIN;
          goto out_bufferfree;
       }
       //HEXprintk("Encrypted buffer is: ",buffer,16); 
       //printk("Output opos=%ld\n",opos);
       if (__kernel_write(tempoutputf, buffer, PAGE_SIZE, &opos) != PAGE_SIZE)
       {
          err=-EIO;
          goto out_bufferfree;
       }
      }
      while(len==PAGE_SIZE);
     
      domd5hash(passphrase,(void *)(preamble.md5key));
      memcpy(preamble.iv,iv,cipheralgooption==AES ? 16 : 8);
      memcpy(preamble.cipheralgo,cipheralgo,strlen(cipheralgo)+1);
     // Endian agnostic 
      preamble.padding=htonl(padcount);
      
      //HEXprintk("Final preamble is: ",&preamble, sizeof(xcrypt_preamble_t));
       
       // Fill preamble at start of the file.
       opos=0;
       if (__kernel_write(tempoutputf,(char *)&preamble, sizeof(xcrypt_preamble_t), &opos) != sizeof(xcrypt_preamble_t))
       {
          err=-EIO;
          goto out_bufferfree;
       }
      
    }
    
    // OK, let's do it
    else if (op==DECRYPT)
    {
     ipos = inputf->f_pos;
     opos = tempoutputf->f_pos;
     old_fs = get_fs();
     set_fs(get_ds());
     len = vfs_read(inputf, (char *)&preamble,sizeof(xcrypt_preamble_t), &ipos);
     set_fs(old_fs);
     //printk("decrypt read preamble len is:%d ",len);
     if (len < 0) 
     {
         err= -EIO;
         goto out_bufferfree;
     }

     if (len != sizeof(xcrypt_preamble_t)) 
     {
         err= -EIO;
         printk("Cannot get preamble to decrypt\n");
         goto out_bufferfree;
     }
     // Password check
     domd5hash(passphrase,keyhash);
     if( memcmp(keyhash,preamble.md5key,16)!=0  )
     {
        err=-EACCES;
        goto out_bufferfree;
     }

     if(strcmp( preamble.cipheralgo,cipheralgo) != 0 )
     {
        printk("cipher algo mismatch\n");
        err=-EINVAL;
        goto out_bufferfree;
     }
     //HEXprintk("Decrypt passphrase is: ",passphrase,16);
     if (crypto_blkcipher_setkey(blkcipher, passphrase, 16 )) {
          printk("key could not be set\n");
          err = -EAGAIN;
          goto out_bufferfree;
      }

     //HEXprintk("IV is: ",preamble.iv,16);
       #ifdef EXTRA_CREDIT
      crypto_blkcipher_set_iv(blkcipher, preamble.iv, cipheralgooption==AES ? 16 : 8);  
       #else
      crypto_blkcipher_set_iv(blkcipher, preamble.iv, 16);  
      #endif

     // Endian agnostic  
      padcount=ntohl(preamble.padding);
      //printk("Padcount is %ld\n",padcount);
     do
     {
       set_fs(get_ds());
       //printk("ipos=%d\n",ipos);
       len = vfs_read(inputf, buffer, PAGE_SIZE, &ipos);
       set_fs(old_fs);
       //printk("read len is:%d\n",len);
       //HEXprintk("buffer is: ",buffer,16);
       
       
       if (len < 0) 
       {
           err= -EIO;
           goto out_bufferfree;
       }
       else if(len>0 && len < PAGE_SIZE)
       {
            printk("Decrypt file not page size block long");
            err=-EIO;
            goto out_bufferfree;
       }

       else if(len==0)
        break;


      /* decrypt data in place */
       if(  crypto_blkcipher_decrypt(&desc, &sg, &sg, PAGE_SIZE) < 0) 
       {
          err=-EAGAIN;
          goto out_bufferfree;
       }
      // HEXprintk("Decrypted buffer is: ",buffer, 16); 
       
         if (__kernel_write(tempoutputf, buffer, PAGE_SIZE, &opos) != PAGE_SIZE)
         {
            err=-EIO;
            goto out_bufferfree;
         }

      }
      while(len==PAGE_SIZE);
     
     // Remove padding
      if(padcount)
      {
        if( (err=vfs_truncate(&tempoutputf->f_path,inputf->f_inode->i_size - sizeof(xcrypt_preamble_t) - padcount)) < 0  )  
            goto out_bufferfree;
      }

    }
    
//    printk("xcrypt received arg input=%s\n",input);
//    printk("xcrypt received arg tempoutput=%s\n",tempoutput);
//    printk("xcrypt received arg op=%d\n",op);
//    printk("xcrypt received arg pass=%s\n",passphrase);
//    printk("xcrypt received arg ino=%ul\n",iv->ino);
//    printk("xcrypt received arg pageno=%ul\n",iv->pageno);
//    printk("xcrypt received arg isb=%ul\n", inputf->f_inode->i_sb->s_dev);

    out_bufferfree:
        if(buffer)
            kfree(buffer);
        if(iv)
            kfree(iv);
        if (blkcipher)
            crypto_free_blkcipher(blkcipher);
    out_fclose:
        // Rename temp to output file. If error, remove temp file
        if(err == 0)
        {
              outputf=filp_open(output,O_CREAT|O_RDWR,outputf_mode);
              if (IS_ERR(tempoutputf))
              {
                  err=-EIO;
              }
           
              else
              {
                lock_rename(tempoutputf->f_path.dentry->d_parent, outputf->f_path.dentry->d_parent );
           
                if( vfs_rename( tempoutputf->f_path.dentry->d_parent->d_inode, 
                       tempoutputf->f_path.dentry,outputf->f_path.dentry->d_parent->d_inode, outputf->f_path.dentry,
                       NULL, 0))
                {
                    err=-EIO;
                    printk("vfs_rename error\n");
                }

                unlock_rename(tempoutputf->f_path.dentry->d_parent, outputf->f_path.dentry->d_parent);
              }
          }
        
        if(err < 0)
        {
            if( tempoutput && (kern_path(tempoutput,LOOKUP_FOLLOW, &tempoutputfpath)) == 0  )
            {
                mutex_lock_nested(&tempoutputfpath.dentry->d_parent->d_inode->i_mutex, I_MUTEX_PARENT);
                vfs_unlink(tempoutputfpath.dentry->d_parent->d_inode, tempoutputfpath.dentry, NULL);
                mutex_unlock(&tempoutputfpath.dentry->d_parent->d_inode->i_mutex);
            }
           // If output file did not exist and was created by us, remove it too. 
            if( ! outputfexisted)
            {
               if( output && (kern_path(output,LOOKUP_FOLLOW, &outputfpath)) == 0  )
               {
                   mutex_lock_nested(&outputfpath.dentry->d_parent->d_inode->i_mutex, I_MUTEX_PARENT);
                   vfs_unlink(outputfpath.dentry->d_parent->d_inode, outputfpath.dentry, NULL);
                   mutex_unlock(&outputfpath.dentry->d_parent->d_inode->i_mutex);
               }
            }
        }
        if(inputf && ! IS_ERR(inputf))
            filp_close(inputf,NULL);
        if(tempoutputf && ! IS_ERR(tempoutputf))
            filp_close(tempoutputf,NULL);
    out_argfree:
        if(input) 
            kfree(input);
        if(output) 
            kfree(output);
        if(tempoutput) 
            kfree(tempoutput);
        if(passphrase) 
            kfree(passphrase);
        if(cipheralgo) 
            kfree(cipheralgo);

    out_last:
        return err;

}

void domd5hash(char * key,char *hash)
{
 struct crypto_shash * md5tfm=NULL;
 struct shash_desc  *md5desc=NULL;
 int rc=0;
 unsigned int size;

 md5tfm=crypto_alloc_shash ("md5",0,0);
     if (IS_ERR(md5tfm)) {
         rc = PTR_ERR(md5tfm);
         goto cleanup;
     }
 size = sizeof(struct shash_desc) + crypto_shash_descsize(md5tfm);

 md5desc = kmalloc(size, GFP_KERNEL);
 md5desc->tfm = md5tfm;
 md5desc->flags = 0x0;
 rc = crypto_shash_init(md5desc);
 if(rc)
 {
     printk("could not init md5\n");
     goto cleanup;
 }
 rc = crypto_shash_update(md5desc, key, 16);
 if(rc)
 {
     printk("could not updTE md5\n");
     goto cleanup;
 }
 rc = crypto_shash_final(md5desc, hash );
 if(rc)
 {
     printk("could not fin md5\n");
     goto cleanup;
 }
     //HEXprintk("key md5 hash: ",hash, 16);

 cleanup:
 if(md5tfm)
     crypto_free_shash ( md5tfm);
 if(md5desc)
     kfree(md5desc);

}

void HEXprintk(const char * msg ,void *scratchpad,int count)
{
    int i;
    printk("%s",msg);
    for(i=0;i<count;i++)
    {
        printk("%02x ",(unsigned char) ((unsigned char *)scratchpad)[i]);
    }
    printk("\n");
}

static int __init init_sys_xcrypt(void)
{
	printk("installed new sys_xcrypt module\n");
	if (sysptr == NULL)
		sysptr = xcrypt;
	return 0;
}
static void  __exit exit_sys_xcrypt(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	printk("removed sys_xcrypt module\n");
}
module_init(init_sys_xcrypt);
module_exit(exit_sys_xcrypt);
MODULE_LICENSE("GPL");
