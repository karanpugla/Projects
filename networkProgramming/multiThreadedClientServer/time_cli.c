#include "unp.h"
#include "common.h"

int
main(int argc, char **argv)
{
        int  sockfd, n,pfd;
        char                            recvline[MAXLINE + 1];
        struct sockaddr_in      servaddr;
        if (argc != 3)
         err_quit("usage: a.out <IPaddress>");
        pfd=atoi(argv[2]);
        //Replace stderr with pipe
        if( dup2(pfd,fileno(stderr)) == -1 )
          err_sys("dup2 error");
        close(pfd);

        if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                err_sys("socket error");
        bzero(&servaddr, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port   = htons(TIME_PORT);        
        if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0)
                err_quit("inet_pton error for %s", argv[1]);

        if (connect(sockfd, (SA *) &servaddr, sizeof(servaddr)) < 0)
                err_sys("connect error");

        while ( (n = read(sockfd, recvline, MAXLINE)) > 0) {
                recvline[n] = 0;        /* null terminate */
                if (fputs(recvline, stdout) == EOF)
                        err_sys("fputs error");
        }
        for(;;)
        {
          if ( (n=Readline(sockfd, recvline, MAXLINE)) == 0)
          {
            err_quit("str_cli: server terminated prematurely");
          }
          if (n < 0)
            err_sys("read error");
          recvline[n] = 0;
          if (fputs(recvline, stdout) == EOF)
            err_sys("fputs error");
        }

        exit(0);
}
