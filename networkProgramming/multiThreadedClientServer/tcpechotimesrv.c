#include "unp.h"
#include "unpthread.h"
#include "common.h"

// Client service specific 
void str_time(int sockfd);
void str_echo(int sockfd);

// Generic thread function, specializes its behavior based on its argument.
static void *doit(void *);

// argument structure to be passed to doit
struct threadarg
{
  int thread_type;
  int connfd;
};

int
main(int argx, char **argv)
{
	int echolistenfd,timelistenfd, connfd, maxfdp1;
  int flags;
  fd_set rset;
	socklen_t clilen;
  pthread_t tid;
  struct threadarg *targ;// temp structure to be filled and passed as argument to doit.
	struct sockaddr_in cliaddr;
	void sig_chld(int signo);
  
  // Create two listening sockets for 2 services.
  // Select on two listening sockets.
  // Allocate argument to create service specific thread
  // Thread gets created and service the client.
  // Thread must free argument which was allocated for it before terminating.

	echolistenfd=Tcp_listen(NULL, ECHO_PORT_STR,NULL);
	timelistenfd=Tcp_listen(NULL, TIME_PORT_STR,NULL);

  // On Solaris sockets returned by accept inherits this flag, be sure to unset before passing to thread
  flags = Fcntl(echolistenfd, F_GETFL, 0);
  Fcntl(echolistenfd, F_SETFL, flags | O_NONBLOCK);
  flags = Fcntl(timelistenfd, F_GETFL, 0);
  Fcntl(timelistenfd, F_SETFL, flags | O_NONBLOCK);
  
  FD_ZERO(&rset);
  for( ; ;)
	{
    FD_SET(echolistenfd,&rset);
    FD_SET(timelistenfd,&rset);
	  clilen = sizeof(cliaddr);
    maxfdp1 = max(echolistenfd, timelistenfd ) + 1;
    
    
    if( select(maxfdp1, &rset, NULL, NULL, NULL) < 0 )
    {
    // Take care if main thread gets interrupted.
     if (errno == EINTR)
       continue;            
     else
       err_sys("select error");
    }
    
    if (FD_ISSET(echolistenfd, &rset))
    {
      if( (connfd = accept(echolistenfd, (SA*) &cliaddr, &clilen)) < 0 )
      {
        if(errno!=EWOULDBLOCK && errno!=ECONNABORTED && errno!=EPROTO)
        {
          err_sys("!! ECHO accept error.\n"); 
        }
      }
      // Instead of using pre-allocated argument structure, argument needs to be allocated dynamically
      // for every thread to avoid race condition.
      else
      {
      //unset O_NONBLOCK
      flags = Fcntl(connfd, F_GETFL, 0);
      Fcntl(connfd, F_SETFL, flags & ~O_NONBLOCK);
      // Thread will free it later after use
      targ=malloc(sizeof(struct threadarg));
      targ->thread_type=ECHO_THREAD_TYPE;
      targ->connfd=connfd;
      Pthread_create(&tid, NULL, &doit, (void *) targ);
      printf("#Creating ECHO thread %d\n", tid);
      }
    }
    
    if (FD_ISSET(timelistenfd, &rset))
    {
      if( (connfd = accept(timelistenfd, (SA*) &cliaddr, &clilen)) < 0 )
      {
        if(errno!=EWOULDBLOCK && errno!=ECONNABORTED && errno!=EPROTO)
        {
          err_sys("!! TIME accept error.\n"); 
        }
      }
      else
      {
      //unset O_NONBLOCK
      flags = Fcntl(connfd, F_GETFL, 0);
      Fcntl(connfd, F_SETFL, flags & ~O_NONBLOCK);
      // Thread will free it later after use
      targ=malloc(sizeof(struct threadarg));
      targ->thread_type=TIME_THREAD_TYPE;
      targ->connfd=connfd;
      Pthread_create(&tid, NULL, &doit, (void *) targ);
      printf("#Creating TIME thread %d\n", tid);
      }
    }
	}
}

static void *
doit(void *arg)
{
 struct threadarg *targ;
 targ=(struct threadarg *)arg;
 Pthread_detach(pthread_self());
 if( targ->thread_type==ECHO_THREAD_TYPE )
 {
  str_echo( targ->connfd ); 
 }
 else if( targ->thread_type==TIME_THREAD_TYPE )
 {
  str_time( targ->connfd );
 }
 printf("!!%s thread %d termination\n",targ->thread_type==ECHO_THREAD_TYPE?STR_ECHO_THREAD_TYPE:STR_TIME_THREAD_TYPE,\
 pthread_self());
 Close(targ->connfd); /* done with connected socket */
 free(arg); // Must be freed here as it was allocated by main thread.
 return (NULL);
}

void
str_echo(int sockfd)
{
        ssize_t         n;
        char            buf[MAXLINE];

again:
        while ( (n = read(sockfd, buf, MAXLINE)) > 0)
        {
         if( writen(sockfd, buf, n)!=n )
         {
          printf("!!Write error. ");
          return;
         }
        }
        if (n < 0 && errno == EINTR)
                goto again;
        else if (n < 0)
                printf("!!Read error. ");
}

void str_time(int sockfd)
{
  struct timeval timeout;
  fd_set rset;
  time_t ticks;
  char buff[MAXLINE];

  FD_ZERO(&rset);
  timeout.tv_sec=5;
  timeout.tv_usec=0;

  do
  {
    ticks = time(NULL);
    snprintf(buff, sizeof(buff), "%.24s\r\n", ctime(&ticks));
    if (writen(sockfd, buff, strlen(buff)) != strlen(buff))
      printf("!!Write error. ");

    FD_SET(sockfd,&rset); 
    // Wait for 5 seconds before sending time again.
    // In TIME server thread,unlike ECHO server thread, we are not expecting anything from client. If socket
    // is set for read, this means client has terminated.
    if( select(sockfd+1,&rset,NULL,NULL,&timeout)<0 )
    {
      printf("!!Select error. ");
      return;
    }
    if(FD_ISSET(sockfd,&rset))
    {
      return;
    }
  }
  while(1);
}
