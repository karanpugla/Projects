#include "unp.h"
#include "common.h"

void str_cli(FILE *fp, int sockfd);

int
main(int argc, char **argv)
{
        int sockfd,pfd;
        struct sockaddr_in      servaddr;
        pfd=atoi(argv[2]);
        // Replace stderr with pipe
        if( dup2(pfd,fileno(stderr)) == -1 )
        {
          err_sys("dup2 error");
        }
        close(pfd);
        if (argc != 3)
                err_quit("usage: tcpcli <IPaddress>");

        sockfd = Socket(AF_INET, SOCK_STREAM, 0);

        bzero(&servaddr, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(ECHO_PORT);
        Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

        Connect(sockfd, (SA *) &servaddr, sizeof(servaddr));

        str_cli(stdin, sockfd);         /* do it all */

        exit(0);
}

void
str_cli(FILE *fp, int sockfd)
{
        int                     maxfdp1, stdineof = 0;
        fd_set          rset;
        char            sendline[MAXLINE], recvline[MAXLINE];

        FD_ZERO(&rset);
        for ( ; ; ) {
                if (stdineof == 0)
                        FD_SET(fileno(fp), &rset);

                FD_SET(sockfd, &rset);
                maxfdp1 = max(fileno(fp), sockfd) + 1;
                Select(maxfdp1, &rset, NULL, NULL, NULL);

                if (FD_ISSET(sockfd, &rset)) {  /* socket is readable */
                        if (Readline(sockfd, recvline, MAXLINE) == 0) {
                                if (stdineof == 1)
                                        return;         /* normal termination */
                                else
                                        err_quit("str_cli: server terminated prematurely");
                        }

                        Fputs(recvline, stdout);
                }

                if (FD_ISSET(fileno(fp), &rset)) {  /* input is readable */
                        if (Fgets(sendline, MAXLINE, fp) == NULL) {
                                stdineof = 1;
                                Shutdown(sockfd, SHUT_WR);      // send FIN, but don't close as server might still be sending 
                                FD_CLR(fileno(fp), &rset);
                                continue;
                        }

                        Writen(sockfd, sendline, strlen(sendline));
                }
        }
}

