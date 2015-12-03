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

int main(int argc, char *argv[])
{
	struct sockaddr_un sock;
	int sfd;
	char mesg[40], ip[20];
	int port;
	time_t ticks;
	char buf[100], src_host[10];
	struct hostent *host;

	sfd = Socket(AF_LOCAL, SOCK_DGRAM, 0);

	/* bind server */
	bzero(&sock, sizeof(sock));
	sock.sun_family = AF_LOCAL;
	unlink(SERVER_SUN_PATH);
	strcpy(sock.sun_path, SERVER_SUN_PATH);
	Bind(sfd, (SA*)&sock, SUN_LEN(&sock));

	printf("sun odr path %s\n", sock.sun_path );
	/* get the current host */
	bzero(src_host, 0);
	gethostname(src_host, 10);
	printf("The current host is %s\n", src_host);

	/* initialize odr to talk to */
	bzero(&odr, sizeof(odr));
	odr.sun_family = AF_UNIX;
	strcpy(odr.sun_path, SUN_PATH_ODR);

	/* listening for clients */
	printf("Listening for clients\n");
	while (1) {
		mesg_recv(sfd, mesg, ip, &port);
		//printf("Received message %s from ip %s and port %d\n", mesg, ip, port);

		/* get source vm requesting time */
		host = gethostbyname(ip);
		if (!host) {
			printf("No host found with ip %s\n", ip);
			continue;
		}

		/* send time to odr */
		bzero(buf, 100);
		ticks = time(NULL);
		snprintf(buf, 100, "%.24s\n", ctime(&ticks));
		printf("server at %s responding to request from %s\n", src_host, host->h_name);
		mesg_send(sfd, ip, port, buf, 0);
	}

	close(sfd);
}
