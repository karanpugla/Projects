#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <xcrypt_common.h>
#include <openssl/md5.h>
#ifndef __NR_xcrypt
#error xcrypt system call not defined
#endif


void print_usage(void *msg);
void HEXprintk(char * msg ,void *scratchpad,int count)
{
  int i;
  printf("%s",msg);
  for(i=0;i<count;i++)
  {
    printf("%02x ",(unsigned char) ((unsigned char *)scratchpad)[i]);
  }
   printf("\f");
}
int main(int argc, char * const argv[])
{
	int rc,eflag=0,dflag=0,pflag=0,cflag=0;
    xcrypt_arg_t  xarg;
	//void *dummy = (void *) argv[1];
    char c;
    opterr=0;
    while( (c=getopt(argc,argv,"edc:p:h"))!= -1  )
    {
       switch(c)
       {
        case 'e':
            if( dflag==0 )
            {
                xarg.op=ENCRYPT;
                eflag=1;
            }
            else
                print_usage("Either -e or -d is required.\n");
        break;

        case 'd':
            if( eflag==0 )
            {
                xarg.op=DECRYPT;
                dflag=1;
            }
            else
                print_usage("Either -e or -d is required.\n");
        break;

        case 'p':
            pflag=1;
            if( strlen(optarg)<6  )
                print_usage("Passphrase must be atleast 6 characters long.\n");
            xarg.passphrase=(char *)MD5((unsigned char *)optarg, strlen(optarg), NULL);
        break;

        case 'c':
	    cflag=1;
            if( strcmp(optarg,"cbc(aes)")==0 || strcmp(optarg,"aes")==0)
                xarg.cipheralgo="cbc(aes)";
            else if( strcmp(optarg,"cbc(blowfish)")==0 || strcmp(optarg,"blowfish")==0)
                xarg.cipheralgo="cbc(blowfish)";
            else 
                print_usage("Only 'cbc(aes)|aes' cipher is supported.\n");
        break;

        case 'h':
            print_usage(NULL);
        break;

        case '?':
            if( optopt=='p' )
                print_usage("Missing argument to -p.\n");
            else if( optopt=='c' )
                print_usage("Missing argument to -c.\n");
            else 
            {
                printf("Unknown option '-%c'\n",optopt);
                print_usage(NULL);
            }
                 
       }         
    }

    if( (eflag||dflag) && pflag  )
    {
        if( optind+1 != argc-1 )
            print_usage("Input/Output file parameters are missing.\n");
    }
    else
        print_usage("Missing options -e|-d or passphrase.\n");

   if(! cflag)
   {
        xarg.cipheralgo="cbc(aes)";
   }
    
    xarg.input=argv[optind];
    xarg.output=argv[optind+1];
                
  	rc = syscall(__NR_xcrypt,(void *) &xarg);
	if (rc == 0)
		printf("syscall returned %d\n", rc);
	else
    {
		printf("syscall returned %d (errno=%d):\n", rc, errno);
        perror(NULL);
    }

	exit(rc);
}

void print_usage(void* msg)
{
    if( msg!=NULL )
        printf("%s",(char *)msg);

    printf("USAGE: xcipher [-h] <-e|-d> [-c aes] -p<passphrase> <input> <output>\n\
    -e|-d:    Encrypt or Decrypt input file\n\
    -c:       Cipher algo(aes, blowfish supported)\n\
    -p:       Atlease 6 character long passphrase\n\
    -h:       Print this usage.\n\
    <input>:  Input filename.\n\
    <output>: Output filename.\n");

    exit(1);
}
