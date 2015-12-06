#ifndef _SYS_SUBMIT_JOB_H_
#define _SYS_SUBMIT_JOB_H_
#include<linux/fs.h>

typedef void    Sigfunc(int);

Sigfunc * Signal(int signo, Sigfunc *func);

// job type
#define ENCRYPT  1
#define DECRYPT  2
#define COMPRESS  3
#define DECOMPRESS  4
#define CHECKSUM  5
#define MAXJOBTYPE 6

// job action
#define ADD  1
#define LIST  2
#define DELETE  3
#define CHANGE  4
#define PRIORITYCHANGE 5
#define MAXJOBACTION 6

// job status
#define STATUS_PENDING  1
#define STATUS_COMPLETE  2
#define MAXJOBSTATUS 3


//jb priority
#define MAXJOBPRIORITY 100
#define MINJOBPRIORITY 1

struct jobRequest_t{
	int action;
	int jobId; // to be assigned in kernel
	int type;
	char inputf[1024];
	char outputf[1024];
	int delInputf;
	char algo[50];
	char passphrase[300];
	int priority;
	int status; // to be assigned in kernel
	char *buf; //Useful for kernel to pass back information
	char checksumResult[33];
	int result;
};

int encrypt_file ( char inp_name[], char oup_name[], char key[], int delInp);
int decrypt_file ( char inp_name[], char oup_name[], char key[], int delInp);
int compress_decompress_file ( char inp_name[], char oup_name[], char alg[], int decomp, int delInp);
int checksum_file ( char inp_name[], char *checksum);

#endif
