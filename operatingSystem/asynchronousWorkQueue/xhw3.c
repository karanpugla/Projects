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


#define ADD_ONE  1
#define LIST_JOBS  2
#define REMOVE_JOB  3
#define ADD_MANY  4

int jobReady_G = 0;

void sig_handler(int signal);
int get_jobs(int filter);
int remove_job(int job_id);
void job_add_one(int type, char *algo);
void job_add_many(int type, char *algo);
void printJob(struct jobRequest_t *job);
void jobTypeStr(int type, char *str);
void jobPriorityStr(int type, char *str);

int main(int argc, const char *argv[])
{
	int rc;
	int test_case = ADD_ONE;

	if(argc > 1){
		if(strcmp(argv[1],"list_jobs") == 0){
			test_case = LIST_JOBS;
		} else if (strcmp(argv[1],"remove_job") == 0){
			test_case = REMOVE_JOB;
		} else if (strcmp(argv[1], "add_one") == 0){
			test_case = ADD_ONE;
		} else if (strcmp(argv[1], "add_many") == 0){
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
			}
			break;
		case ADD_MANY:
			job_add_many(COMPRESS, "deflate");
			//job_add_many(CHECKSUM, "lz4"); // zlib is not supported only lzo, lz4 and deflate
			//job_add_many(CHECKSUM, "deflate");
			//job_add_many(ENCRYPT, "cbc(aes)");
			for(;;){
				if (jobReady_G){
					jobReady_G = 0;
					//printf("interrupt was called\n");
				}
				sleep(5);
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
		printf("Job with id: %d does not exist or has already executed. Cannot remove.\n", job_id);
	} else {
		printf("Pending Job with id: %d removed successfully.\n", job_id);
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
		printf("Unable to change priority for job Id: %d\n, Error: %d", job_id, err);
	} else {
		printf("Priority for job id: %d changed successfully.\n", job_id);
	}
	return err;
}

int get_jobs(int filter){
	int err, ret;
	struct jobRequest_t job;
	job.action = LIST;
	job.buf = malloc(4096);
	job.type = filter;
	if(!job.buf){
		//TODO: handle error;
	}
	err = syscall(__NR_submitjob, &job);
	ret = write(1, job.buf, strlen(job.buf));
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
	job.priority = NORMAL_PRIORITY;
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
	if (rc < 0)
		printf("syscall returned %d (errno=%d)\n", rc, errno);
	else{
		job.jobId = rc;
		printJob(&job);
	}
}

void job_add_many(int type, char *algo){
	
	int i, rc;
	struct jobRequest_t job;
	char str[100], nrs[][5] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};

	job.action = ADD;
	for (i=0;i<9;i++){
		if (jobReady_G){
			jobReady_G = 0;
			printf("Signal Handler was called\n");
			//get_jobs(1);
		}
		job.type = type;
		job.priority = NORMAL_PRIORITY;
		if (type == ENCRYPT) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); strcat(str, nrs[i]);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_encrypt"); strcat(str, nrs[i]);
			strcpy(job.outputf, str);
		}
		else if (type == DECRYPT){
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_encrypt"); strcat(str, nrs[i]);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_decrypt"); strcat(str, nrs[i]);
			strcpy(job.outputf, str);
		
		}
		else if (type == COMPRESS) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); strcat(str, nrs[i]);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_compress"); strcat(str, nrs[i]);
			strcpy(job.outputf, str);
		}
		else if (type == DECOMPRESS) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_compress"); strcat(str, nrs[i]);
			strcpy(job.inputf, str);
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile_decompress"); strcat(str, nrs[i]);
			strcpy(job.outputf, str);
		}
		else if (type == CHECKSUM) {
			strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); strcat(str, nrs[i]);
			strcpy(job.inputf, str);
		}
		strcpy(job.passphrase, "5f4dcc3b5aa765d61d8327deb882cf99");
		strcpy(job.algo, algo);
		job.delInputf = 0;
		rc = syscall(__NR_submitjob, &job);
		if (rc < 0)
			printf("syscall returned %d (errno=%d)\n", rc, errno);
		else {
			job.jobId = rc;
			printJob(&job);
		}
	}
	
	job.type = type;
	job.priority = HIGH_PRIORITY;
	strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ifile"); strcat(str, nrs[i]);
	strcpy(job.inputf, str);
	strcpy(str, "/usr/src/hw3-cse506g19/hw3/tmp/ofile"); strcat(str, nrs[i]);
	strcpy(job.outputf, str);
	strcpy(job.passphrase, "5f4dcc3b5aa765d61d8327deb882cf99");
	strcpy(job.algo, algo);
	job.delInputf = 0;
	rc = syscall(__NR_submitjob, &job);
	if (rc < 0)
		printf("syscall returned %d (errno=%d)\n", rc, errno);
	else {
		job.jobId = rc;
		printJob(&job);
	}
	//sleep(5);
}
void printJob(struct jobRequest_t *job){

	char type[20], priority[20];

	jobTypeStr(job->type, type);
	jobPriorityStr(job->priority, priority);
	printf("Added job:\
			\n\tID := %d\
			\n\tType := %s\
			\n\tPriority := %s\
			\n\tInput := %s", job->jobId, type, priority, job->inputf);

	if(job->type != CHECKSUM){
		printf("\n\tOutput := %s", job->outputf);
	}
	
	printf("\n\tDelInput := %d", job->delInputf);
	if (job->type == ENCRYPT || job->type == DECRYPT){
		printf("\n\tPassphrase := %s", job->passphrase);	
		printf("\n\tAlgo := %s", job->algo);
	}
	if (job->type == COMPRESS){
		printf("\n\tAlgo := %s", job->algo);
	}
	printf("\n");
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
void jobPriorityStr(int type, char *str){
	switch(type){
		case NORMAL_PRIORITY:
			strcpy(str, "NORMAL");
			break;
		case HIGH_PRIORITY:
			strcpy(str, "HIGH");
			break;
		default:
			strcpy(str, "UNKNOWN");
	}
}
