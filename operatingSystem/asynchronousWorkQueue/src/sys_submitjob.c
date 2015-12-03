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
#include <linux/spinlock.h>

struct work_params {
	struct jobRequest_t *jobRequest;
	struct task_struct *task;
	struct work_struct work;
	struct pid *pid;
};

spinlock_t workStatusQueueLock;
struct workStatusQueue_t {
	struct list_head list;
	struct jobRequest_t *jobRequest;
	struct work_struct *work;
};


struct workqueue_struct *common_queue;
struct workqueue_struct *common_priority_queue;
struct workStatusQueue_t workStatusQueue;

static int copy_work_status(int filter, char *buf, int bufsize);
static int delete_job(int job_id);
static int change_priority(int job_id, int priority);
asmlinkage extern long (*sysptr)(void *args, int argslen);

int job_encrypt(char inputf[], char outputf[], char algo[], char passphrase[], int delInp)
{
	return encrypt_file(inputf, outputf, passphrase, delInp);
}

int job_decrypt(char inputf[], char outputf[], char algo[], char passphrase[], int delInp)
{
	return decrypt_file(inputf, outputf, passphrase, delInp);
}

int job_checksum(char inputf[], char *checksum)
{
	return checksum_file(inputf, checksum);
}

int job_compress_decompress(char inputf[], char outputf[], char alg[], int decomp, int delInp)
{
	return compress_decompress_file(inputf, outputf, alg, decomp, delInp);
}

int job_ids = 0;

static void print_worker(struct work_struct *work)
{
	int err;
	struct work_params *params;
	struct task_struct *task = NULL;

	err = 0;

	printk("Worker called\n");
	params = container_of(work, struct work_params, work);
	printk("job action=%d\n", params->jobRequest->action);
	printk("job type=%d\n", params->jobRequest->type);
	printk("job inputf=%s\n", params->jobRequest->inputf);
	printk("job outputf=%s\n", params->jobRequest->outputf);


	if (params->jobRequest->type == ENCRYPT) {
		err = job_encrypt(params->jobRequest->inputf,
							params->jobRequest->outputf,
							params->jobRequest->algo,
							params->jobRequest->passphrase,
							params->jobRequest->delInputf);
	} else if (params->jobRequest->type == DECRYPT) {
		err = job_decrypt(params->jobRequest->inputf,
							params->jobRequest->outputf,
							params->jobRequest->algo,
							params->jobRequest->passphrase,
							params->jobRequest->delInputf);
	} else if (params->jobRequest->type == COMPRESS) {
		err = job_compress_decompress(params->jobRequest->inputf,
							params->jobRequest->outputf,
							params->jobRequest->algo, 0,
							params->jobRequest->delInputf);
	} else if (params->jobRequest->type == DECOMPRESS) {
		err = job_compress_decompress(params->jobRequest->inputf,
							params->jobRequest->outputf,
							params->jobRequest->algo, 1,
							params->jobRequest->delInputf);
	} else if (params->jobRequest->type == CHECKSUM) {
		err = job_checksum(params->jobRequest->inputf, params->jobRequest->checksumResult);
	} else{

	}

	spin_lock(&workStatusQueueLock);
	if (err >= 0) {
		printk("print_worker err=%d\n", err);
		params->jobRequest->status = STATUS_COMPLETE;
		params->jobRequest->result = err;
	} else {
		printk("print_worker err=%d\n", err);
		params->jobRequest->status = -EAGAIN;
	}
	spin_unlock(&workStatusQueueLock);

//	if((task = pid_task(params->pid, PIDTYPE_PID)) == NULL) {
//	if((task = pid_task(params->pid, PIDTYPE_PID)) == NULL) {
//		printk("Cannot get task from pid.\n");
//		put_pid(params->pid);
//		return;
//	}
	task = get_pid_task(params->pid, PIDTYPE_PID);
	if (task == NULL) {
		printk("task is null\n");
	}
	else {
		printk("task is not null\n");
		send_sig(SIGIO, task, 0);
		put_task_struct(task);
	}
	

//	printk("worker task->pid=%d\n", task->pid);
	//send_sig(SIGIO, task, 0);
	put_pid(params->pid);
}

asmlinkage long submitjob(void *arg, int argslen)
{
	struct workStatusQueue_t *tmp = NULL;
	int error = 0;
	struct work_params *work = NULL;
	struct jobRequest_t *job = NULL;


	if (!arg) {
		error = -EINVAL;
		goto err;
	}
	job = kzalloc(sizeof(struct jobRequest_t), GFP_KERNEL);

	if (!job) {
		error = -ENOMEM;
		goto err;
	}
	if (copy_from_user(job, arg, sizeof(struct jobRequest_t))) {
		printk("cannot copy job from user.\n");
		error = -EFAULT;
		goto err;
	}


	if (job->action == ADD) {
		printk("submit job called\n");
		work = kmalloc(sizeof(struct work_params), GFP_KERNEL);
		tmp = (struct workStatusQueue_t *)kmalloc(sizeof(struct workStatusQueue_t), GFP_KERNEL);

		if (!work || !tmp) {
			error = -ENOMEM;
			goto err;
		}
		//Validate all user input and add job request to work below:
		if (job->type < 0 || job->type >= MAXJOBTYPE) {
			printk("Unsupported Job type.\n");
			error = -EINVAL;
			goto err;
		}
		if (job->priority < 0 || job->priority >= MAXJOBPRIORITY) {
			printk("Unsupported Job priority.\n");
			error = -EINVAL;
			goto err;
		}
		if (job->action < 0 || job->action >= MAXJOBACTION) {
			printk("Unsupported Job action.\n");
			error = -EINVAL;
			goto err;
		}

		job->status = STATUS_PENDING;
		job->jobId = job_ids++;
		work->jobRequest = job;
		work->pid = get_task_pid(current, PIDTYPE_PID);
		printk("work->pid=%p, current.pid=%d\n", work->pid, current->pid);
//		if (work->pid == NULL) {
//			printk("cannot get current task's struct pid\n");
//			error = -EAGAIN;
//			goto err;
//		}
		tmp->jobRequest = job;
		// Put same pointer in both the above assignments.


		work->task = current;
		INIT_WORK(&work->work, print_worker);
		if (job->priority == NORMAL_PRIORITY) {
			queue_work(common_queue, &work->work);
		} else if (job->priority == HIGH_PRIORITY){
			queue_work(common_priority_queue, &work->work);
		}
		tmp->work = &work->work;
		spin_lock(&workStatusQueueLock);
		//get spinlock
		list_add_tail(&(tmp->list), &(workStatusQueue.list));
		spin_unlock(&workStatusQueueLock);
		//release spinlock
		error = job->jobId;
	} else if (job->action == DELETE) {
		error = delete_job(job->jobId);
	} else if(job->action == LIST) {
		printk("List jobs called.\n");
		copy_work_status(job->type, job->buf, 4096);
	} else if(job->action == PRIORITYCHANGE){
		printk("priority change called.\n");
		error = change_priority(job->jobId, job->priority);	
	} else {
		printk("Unknown job action.\n");
		error = -EINVAL;
	}

err:
	if (error < 0) {
		if (work)
			kfree(work);
		if (job)
			kfree(job);
		if (tmp)
			kfree(tmp);
	}
	return error;
}

static int change_priority(int job_id, int priority){
	struct workStatusQueue_t *tmp;
	struct list_head *pos, *q;
	int error = 0;
	struct work_params *work = NULL;
	spin_lock(&workStatusQueueLock);
	list_for_each_safe(pos, q, &(workStatusQueue.list)) {
                tmp = list_entry(pos, struct workStatusQueue_t, list);
                if (tmp->jobRequest->jobId == job_id && tmp->jobRequest->priority != priority) {
                        //if(tmp->jobRequest->status == STATUS_PENDING) {
				printk("Camehere1\n");
                                error = cancel_work_sync(tmp->work);
                                printk("Removed job from queue: %d\n", error);
                                if(error == 1){
                                        //list_del(pos);
                                        //kfree(tmp->jobRequest);
                                        //kfree(tmp);
					printk("Camehere2\n");
					tmp->jobRequest->priority = priority;
					work = kmalloc(sizeof(struct work_params), GFP_KERNEL);
					if(!work){
						error = -ENOMEM;
						goto out_release_lock;
					}
					work->jobRequest = tmp->jobRequest;
					work->task = current;
					INIT_WORK(&work->work, print_worker);
                			if (priority == NORMAL_PRIORITY) {
                        			queue_work(common_queue, &work->work);
                			} else if (priority == HIGH_PRIORITY){
                        			queue_work(common_priority_queue, &work->work);
                			} else {
						error = -EINVAL;
						goto out_free_work;
					}
                			tmp->work = &work->work;
                                } else {
					printk("Camehere3\n");
					error = -EINVAL;
					goto out_release_lock;
				}
				break;
                        //}
                } else {
			error = -EINVAL;
		}
        }
        //release spinlock
	out_free_work:
		if(error < 0){
			kfree(work);
		}
	out_release_lock:
        	spin_unlock(&workStatusQueueLock);
       // out:
		return error;


}

static int delete_job(int job_id) 
{
	struct workStatusQueue_t *tmp;
	struct list_head *pos, *q;
	int error = 0;
	//get spinlock
	spin_lock(&workStatusQueueLock);
	list_for_each_safe(pos, q, &(workStatusQueue.list)) {
		tmp = list_entry(pos, struct workStatusQueue_t, list);
		if (tmp->jobRequest->jobId == job_id) {
			//if(tmp->jobRequest->status == STATUS_PENDING) {
				error = cancel_work_sync(tmp->work);
				printk("Removed job from queue: %d\n", error);
				if(error == 1){
					list_del(pos);
					kfree(tmp->jobRequest);
					kfree(tmp);
				}
				break;
			//}
		}
	}
	//release spinlock
	spin_unlock(&workStatusQueueLock);
	return error;
}

static int copy_work_status(int filter, char *buf, int bufsize)
{
	struct workStatusQueue_t *tmp;
	struct list_head *pos, *q;
	char ker_buf[100];
	char *arg_counter;
	arg_counter = buf;

	spin_lock(&workStatusQueueLock);
	list_for_each_safe(pos, q, &(workStatusQueue.list)) {
		tmp = list_entry(pos, struct workStatusQueue_t, list);
		if (filter == 0 || (filter == 1 && (tmp->jobRequest->status == STATUS_COMPLETE || tmp->jobRequest->status < 0))) {
			if (tmp->jobRequest->status < 0) {
				snprintf(ker_buf, 100, "Job Id: %d, ERROR: %d\n", tmp->jobRequest->jobId, tmp->jobRequest->status);
			} else {
				if(tmp->jobRequest->type == COMPRESS){
					snprintf(ker_buf, 100,"Job Id: %d, Compressed File Size: %d\n", tmp->jobRequest->jobId, tmp->jobRequest->result);
				} else if(tmp->jobRequest->type == DECOMPRESS){
					snprintf(ker_buf, 100,"Job Id: %d, Decompressed File Size: %d\n", tmp->jobRequest->jobId, tmp->jobRequest->result);
				} else if(tmp->jobRequest->type == ENCRYPT){
					snprintf(ker_buf, 100,"Job Id: %d, Encryption successful\n", tmp->jobRequest->jobId);
				} else if(tmp->jobRequest->type == DECRYPT){
					snprintf(ker_buf, 100,"Job Id: %d, Decryption successful\n", tmp->jobRequest->jobId);
				}else if(tmp->jobRequest->type == CHECKSUM){
					snprintf(ker_buf, 100,"Job Id: %d, File Checksum: %s\n", tmp->jobRequest->jobId, tmp->jobRequest->checksumResult);
				}
				
			}
			if (!copy_to_user(arg_counter, ker_buf, strlen(ker_buf))) {
				arg_counter += strlen(ker_buf);
				if (tmp->jobRequest->status == STATUS_COMPLETE || tmp->jobRequest->status < 0) {
					printk("Removed job from queue\n");
					list_del(pos);
					kfree(tmp->jobRequest);
					kfree(tmp);
				}
			} else {
				printk("copy to user error\n");
			}
		}
	}
	//release spinlock
	spin_unlock(&workStatusQueueLock);
	return 0;
}

static int __init init_sys_submitjob(void)
{
	printk("installed new sys_xcrypt module\n");
	if (sysptr == NULL)
		sysptr = submitjob;

	common_queue = create_workqueue("async_events");
	common_priority_queue = alloc_workqueue("async_priority_events", WQ_MEM_RECLAIM | WQ_HIGHPRI, 1);
	INIT_LIST_HEAD(&workStatusQueue.list);
	spin_lock_init(&workStatusQueueLock);
	return 0;
}
static void  __exit exit_sys_submitjob(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	destroy_workqueue(common_queue);
	destroy_workqueue(common_priority_queue);
	printk("removed sys_submitjob module\n");
}







module_init(init_sys_submitjob);
module_exit(exit_sys_submitjob);
MODULE_LICENSE("GPL");
