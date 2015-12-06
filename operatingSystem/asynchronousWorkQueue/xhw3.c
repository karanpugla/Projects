#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include "common.h"
#ifndef __NR_submitjob
#error submitjob system call not defined
#endif

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"


#define ADD_ONE  1
#define LIST_JOBS  2
#define REMOVE_JOB  3
#define ADD_MANY  4


int jobReady_G = 0;

void sig_handler(int signal);
int get_jobs(int filter);
int remove_job(int job_id);
void job_add_one(int type, char *algo);
void job_add_many(char *algo, int count, int stage);
void printJob(struct jobRequest_t *job);
void jobTypeStr(int type, char *str);
void jobPriorityStr(int type, char *str);
int main(int argc, const char *argv[])
{
	int rc;
	int test_case = ADD_ONE;
	int jobCount = 10;
	int stage = 1;
	int wait = 0;

	if(argc > 1){
		if(strcmp(argv[1],"list_jobs") == 0){
			test_case = LIST_JOBS;
		} else if (strcmp(argv[1],"remove_job") == 0){
			test_case = REMOVE_JOB;
		} else if (strcmp(argv[1], "add_one") == 0){
			test_case = ADD_ONE;
		} else if (strcmp(argv[1], "add_many") == 0){
			if(argc != 5) {
				printf(ANSI_COLOR_RED "Insufficient arguments. job count, stage and wait expected.\n");
				printf(ANSI_COLOR_RESET);
				fflush(stdout);
			}
			else {
				jobCount = atoi(argv[2]);
				stage = atoi(argv[3]);
				wait = atoi(argv[4]);
			}
			test_case = ADD_MANY;
		}

	}


	
	Signal(29,&sig_handler);

	switch(test_case){
		case ADD_ONE:
			//job_add_one(CHECKSUM, "lz4"); // zlib is not supported only lzo, lz4 and deflate
			job_add_one(COMPRESS, "lz4"); // zlib is not supported only lzo, lz4 and deflate
			//job_add_one(DECOMPRESS, "lz4"); // zlib is not supported only lzo, lz4 and deflate
			//job_add_one(ENCRYPT, "(cbc(aes))"); 
			sleep(5);
			if(jobReady_G){
				jobReady_G = 0;
				printf("sig handler was called\n");
				printf(ANSI_COLOR_RESET);
				fflush(stdout);
			}
			break;
		case ADD_MANY:
			job_add_many("deflate", jobCount, stage);
			if(wait == 1){
				for(;;){
					if (jobReady_G){
						jobReady_G = 0;
					}
        				sleep(5);
				}
			}
			break;
		case 2:
			get_jobs(0);
			break;
		case 3:
			remove_job(atoi(argv[2]));
			break;

	}

	return rc;//exit(rc);
}

int remove_job(int job_id){
	int err;
	struct jobRequest_t job;
	job.action = DELETE;
	job.jobId = job_id;
	err = syscall(__NR_submitjob, &job);
	if(err == 0){
		printf(ANSI_COLOR_BLUE "Job with id: %d does not exist or has already executed. Cannot remove.\n", job_id);
		printf(ANSI_COLOR_RESET);
				fflush(stdout);
	} else {
		printf(ANSI_COLOR_BLUE "Pending Job with id: %d removed successfully.\n", job_id);
		printf(ANSI_COLOR_RESET);
				fflush(stdout);
	}
	return err;	
}

int change_priority(int job_id, int new_priority){
	int err;
	struct jobRequest_t job;
	job.action = PRIORITYCHANGE;
	job.jobId = job_id;
	job.priority = new_priority;
	err = syscall(__NR_submitjob, &job);
	if(err <= 0){
		printf(ANSI_COLOR_CYAN "Unable to change priority for job Id: %d\n, Error: %d", job_id, err);
		printf(ANSI_COLOR_RESET);
		fflush(stdout);
	} else {
		printf(ANSI_COLOR_CYAN "Priority for job id: %d changed successfully.\n", job_id);
		printf(ANSI_COLOR_RESET);
		fflush(stdout);
	}
	return err;
}

int get_jobs(int filter){
	int err;
	struct jobRequest_t job;
	job.action = LIST;
	job.buf = malloc(4096);
	memset(job.buf,0,4096);
	job.type = filter;
	if(!job.buf){
		//TODO: handle error;
	}
	err = syscall(__NR_submitjob, &job);
	printf(ANSI_COLOR_MAGENTA "%s", job.buf);
	printf(ANSI_COLOR_RESET);
	free(job.buf);
	return err;
}
void sig_handler(int signal) {
	jobReady_G = 1;
	get_jobs(1);
}

void job_add_one(int type, char *algo){

	struct jobRequest_t job;
	char str[100];
	int rc;

	job.action = ADD;
	job.type = type;
	job.priority = 50;
		if (type == ENCRYPT) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); 
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_encrypt"); 
			strcpy(job.outputf, str);
		}
		else if (type == DECRYPT){
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_encrypt"); 
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_decrypt"); 
			strcpy(job.outputf, str);
		
		}
		else if (type == COMPRESS) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); 
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_compress"); 
			strcpy(job.outputf, str);
		}
		else if (type == DECOMPRESS) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_compress"); 
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_decompress"); 
			strcpy(job.outputf, str);
		}
		else if (type == CHECKSUM) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); 
			strcpy(job.inputf, str);
		}
	strcpy(job.passphrase, "5f4dcc3b5aa765d61d8327deb882cf99");
	strcpy(job.algo, algo);
	job.delInputf = 0;
	rc = syscall(__NR_submitjob, &job);
	if (rc < 0) {
		printf(ANSI_COLOR_RED "syscall returned %d (errno=%d)\n", rc, errno);
		printf(ANSI_COLOR_RESET);
	}
	else{
		job.jobId = rc;
		printJob(&job);
	}
}

void job_add_many(char *algo, int count, int stage){
	
	int i, rc, typeControl = 1;
	struct jobRequest_t job;
	char num[4], str[100];
	
	printf(ANSI_COLOR_GREEN "Adding %d jobs. Stage=%d\n", count, stage);
	printf(ANSI_COLOR_RESET);
				fflush(stdout);
	job.action = ADD;
	for (i=1;i <= count;i++){
		if (jobReady_G){
			jobReady_G = 0;
		}
		if (stage == 1) {
			switch(typeControl++){
				case 1:
					job.type = ENCRYPT;
					break;
				case 2:
					job.type = COMPRESS;
					break;
				case 3:
					job.type = CHECKSUM;
					//job.type = COMPRESS;
					break;
			}
			if (typeControl > 3){
				typeControl = 1;
			}
		}
		else if (stage == 2){
			switch(typeControl++){
				case 1:
					job.type = DECRYPT;
					break;
				case 2:
					job.type = DECOMPRESS;
					break;
			}
			if (typeControl > 2){
				typeControl = 1;
			}
		}

		job.priority = (count - i)%100;
		sprintf(num, "%d", i);
		if (job.type == ENCRYPT) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); strcat(str, num);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_encrypt"); strcat(str, num);
			strcpy(job.outputf, str);
		}
		else if (job.type == DECRYPT){
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_encrypt"); strcat(str, num);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_decrypt"); strcat(str, num);
			strcpy(job.outputf, str);
		}
		else if (job.type == COMPRESS) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); strcat(str, num);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_compress"); strcat(str, num);
			strcpy(job.outputf, str);
		}
		else if (job.type == DECOMPRESS) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_compress"); strcat(str, num);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_decompress"); strcat(str, num);
			strcpy(job.outputf, str);
		}
		else if (job.type == CHECKSUM) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); strcat(str, num);
			strcpy(job.inputf, str);
		}
		strcpy(job.passphrase, "5f4dcc3b5aa765d61d8327deb882cf99");
		strcpy(job.algo, algo);
		job.delInputf = 0;
		rc = syscall(__NR_submitjob, &job);
		if (rc < 0) {
			printf(ANSI_COLOR_RED "syscall returned %d (errno=%d)\n", rc, errno);
			printf(ANSI_COLOR_RESET);
				fflush(stdout);
		}
		else {
			job.jobId = rc;
			printJob(&job);
		}
	}
	
	//sleep(5);
	remove_job(7);
	change_priority(6,1);
	change_priority(8,1);
}
void printJob(struct jobRequest_t *job){

	char type[20];

	jobTypeStr(job->type, type);
	printf(ANSI_COLOR_GREEN "Added job:\
                       \n\tID := %d\
                       \n\tType := %s\
                       \n\tPriority := %d\
                       \n\tInput := %s", job->jobId, type, job->priority, job->inputf);

	printf(ANSI_COLOR_RESET);
				fflush(stdout);
	if(job->type != CHECKSUM){
		printf( ANSI_COLOR_GREEN "\n\tOutput := %s", job->outputf);
	}
	printf(ANSI_COLOR_RESET);
				fflush(stdout);
	
	printf(ANSI_COLOR_GREEN "\n\tDelInput := %d", job->delInputf);
	printf(ANSI_COLOR_RESET);
	if (job->type == ENCRYPT || job->type == DECRYPT){
		//printf("\n\tPassphrase := %s", job->passphrase);	
		printf(ANSI_COLOR_GREEN"\n\tAlgo := %s", job->algo);
		printf(ANSI_COLOR_RESET);
				fflush(stdout);
	}
	if (job->type == COMPRESS){
		printf(ANSI_COLOR_GREEN "\n\tAlgo := %s", job->algo);
		printf(ANSI_COLOR_RESET);
				fflush(stdout);
	}
	printf("\n");
	fflush(stdout);
}

void jobTypeStr(int type, char *str){
	switch(type){
		case ENCRYPT:
			strcpy(str, "ENCRYPT");
			break;
		case DECRYPT:
			strcpy(str, "DECRYPT");
			break;
		case COMPRESS:
			strcpy(str, "COMPRESS");
			break;
		case DECOMPRESS:
			strcpy(str, "DECOMPRESS");
			break;
		case CHECKSUM:
			strcpy(str, "CHECKSUM");
			break;
		default:
			strcpy(str, "UNKNOWN");
	}
}
