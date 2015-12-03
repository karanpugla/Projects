#include "unp.h"
#include <stdio.h>
#include <stdlib.h>
#include "common.h"

struct sockaddr_un odr;

int mesg_recv(int sockfd, char *mesg, char *src_ip, int *src_port)
{
	struct clientRespMsg rply;
	int ret;

	ret = recvfrom(sockfd, &rply, sizeof(rply), 0, NULL, NULL);
	if (ret < 0) {
		printf("recvfrom error\n");
		return ret;
	}
	strcpy(mesg, rply.data);
	strcpy(src_ip, rply.src);
	*src_port = rply.port;

	return ret;
}

int mesg_send(int sockfd, char *dst_ip, int dst_port, char *mesg, int redis)
{
	struct clientReqMsg *msg;
	char rlpy[50], src_ip[17];
	int ret = 0, src_port;

	/* alloc memory for msg */
	msg = (struct clientReqMsg *)malloc(sizeof(struct clientReqMsg));
	if (!msg) {
		printf("Memory exhausted\n");
		goto out;
	}

	/* fill in msg struct */
	strcpy(msg->dest, dst_ip);
	msg->port = dst_port;
	strcpy(msg->data, mesg);
	msg->flag = redis;

	printf("Sending message to %s\n", dst_ip);
	Sendto(sockfd, msg, sizeof(struct clientReqMsg), 0, (SA *)&odr, sizeof(odr));

out:
	free(msg);
	return ret;
}

void handle_alarm(int signo)
{
	return;
}

int main(int argc, char *argv[])
{
	struct sockaddr_un cli;
	int clifd;
	struct clientReqMsg msg;
	char tmpfile[30];
	int vm;
	char buf[5], vmstr[8];
	struct hostent *host;
	char dest[30], src_host[10];
	int ret = 0;
	int attempted = 0;
	char src_ip[16], mesg[50], str[100];
	int src_port;

	clifd = Socket(AF_UNIX, SOCK_DGRAM, 0);

	/* create tempfile */
	bzero(tmpfile, 30);
	strcpy(tmpfile, "/tmp/XXXXXX");
	mkstemp(tmpfile);
	printf("Temporary file %s created\n", tmpfile);

	/* bind client */
	bzero(&odr, sizeof(odr));
	cli.sun_family = AF_UNIX;
	strcpy(cli.sun_path,  tmpfile);
	unlink(tmpfile);
	Bind(clifd, (SA*)&cli, SUN_LEN(&cli));

	/* initialize odr to talk to */
	bzero(&odr, sizeof(odr));
	odr.sun_family = AF_UNIX;
	strcpy(odr.sun_path, SUN_PATH_ODR);

	/* choose a vm */
	do {
		printf("Choose a vm NUMBER to ask time [1/2/3/../10](e.g. 5) and 0 to quit:");
		scanf("%s", str);
		vm = atoi(str);
	} while (!(vm <= 10 && vm >= 0));

	if (!vm) {
		if (!strcmp(str, "0"))
			printf("You chose to quit\n");
		else
			printf("Invalid input. Quitting\n");
		goto out;
	}
	bzero(buf, 0);
	sprintf(buf, "%d", vm);

	/* Get vm details like IP etc. */
	bzero(vmstr, 0);
	strcpy(vmstr, "vm");
	strcat(vmstr, buf);
	printf("You chose %s\n", vmstr);
	host = gethostbyname(vmstr);
	if (!host) {
		printf("no host found\n");
		goto out;
	}
	if (!host->h_addr_list) {
		printf("No address found for %s\n", vmstr);
		goto out;
	}
	bzero(dest, 0);
	inet_ntop(AF_INET, (void *)host->h_addr_list[0], dest, 30);
	printf("%s has ip %s\n", vmstr, dest);

	/* get the current host */
	bzero(src_host, 0);
	gethostname(src_host, 10);
	printf("The current host is %s\n", src_host);

	/* initialize alarm */
	Signal(SIGALRM, handle_alarm);

	/*send message to odr */
send:
	printf("Client at %s sending request to server at %s (attempt %d\n)",
		   src_host, vmstr, attempted);
	mesg_send(clifd, dest, 0, "Time?", attempted);
	alarm(15);

	//printf("Receiving message for attempt %d\n", attempted);
	if (mesg_recv(clifd, mesg, src_ip, &src_port) < 0) {
		if (errno = EINTR) {
			printf("Client at %s: timeout on response from %s\n", src_host, vmstr);
			if (attempted >= 1) {
				printf("attempted twice\n");
				ret = 1;
				goto out;
			} else {
				attempted++;
				goto send;
			}
		} else
			printf("recvfrom error\n");
	}

	//printf("Received message %s from ip %s and port %d", mesg, src_ip, src_port);
	printf("Client at %s: received from %s %s", src_host, vmstr, mesg);

out:
	close(clifd);
	exit(ret);
}
