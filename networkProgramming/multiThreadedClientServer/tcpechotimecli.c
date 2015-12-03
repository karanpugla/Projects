#include "unp.h"

// Signal handlers
// If user terminates parent while child is active, parent kills child before it terminates itself;
void sig_cleanup(int signo); 
// Clean terminated child process
void sig_chld(int signo); 

void fork_child(char *child,struct in_addr ip);

pid_t childpid=NULL;


int main(int argc, char **argv)
{
 size_t n;
 char choice[MAXLINE], *servaddr;
 struct hostent *hptr;
 struct in_addr ip;

 if(argc != 2)
 {
  printf("Usage: client <IP address/FQDN>\nExample: client 127.0.0.1 or client compserv.cs.sunysb.edu\n");
  exit(1);
 }
 servaddr=argv[1];
 if( inet_aton(servaddr,&ip)==0 )
 {
  // inet_aton could not convert, user might have entered FQDN instead of IP
  if( (hptr=gethostbyname(servaddr)) == NULL)
  {
  //Nope, neither IP nor FQDN
    printf("!!Your input server address is either invalid IP or unresolvable domain name.\n");
    exit(1);
  }
  else
  {
  //Now I have both name and IP
    memcpy(&ip.s_addr, *(hptr->h_addr_list), sizeof (ip.s_addr));
    printf("#Server is %s(%s)\n",servaddr,inet_ntoa(ip));
  }
 }
 else
 {
 //Seems like an IP, let's try to resolve into a name
  if((hptr=gethostbyaddr(&ip, 4, AF_INET)) == NULL)
  {
  //IP is valid but cannot resolve
    printf("#Server is <No name found>(%s)\n",servaddr);
  }
  else
  {
  //Now I have both name and IP
  printf("#Server is %s(%s)\n",hptr->h_name,servaddr);
  }
 }

 Signal(SIGINT, sig_cleanup);
 Signal(SIGCHLD, sig_chld); 

 for( ; ; )
 {
  printf("#This is a TCP client.\n#There are 2 services you can request.\n#Which one do you want?\n\
 1.echo\n 2.time\n> ");

  n=Readline(fileno(stdin), choice, MAXLINE);
  //Remove trailing newline from choice and compare
  choice[n-1]='\0';
  printf("#You selected %s\n",choice);
  
  if(strcmp(choice,"echo")==0)
  {
    fork_child("./echo_cli",ip);
  }

  else if(strcmp(choice,"time")==0)
  {
    fork_child("./time_cli",ip);
  }
  else
  {
    printf("!!This service is not in the list. Try again\n");   
  }
 }

 return 0;
}

void fork_child(char *child, struct in_addr ip)
{
   int pfd[2];
   char strpfd[2]; // Will hold one end of pfd number as a string to be passed to child.
   char  childstatus[MAXLINE];
   fd_set rset;
   int maxfdp1;

   if(pipe(pfd) == -1)
   {
     printf("!!cannot create pipe. Abort.\n");
     exit(1);
   }
   // pfd[1] is write end, used by child.
   // pfd[0] is read end, used by parent.
   
   // Convert pfd[1] to string
   strpfd[0]=48+pfd[1];strpfd[1]='\0';
   
   if( (childpid=fork())<0  )
   {
     err_sys("!!fork error");
   }
   else if(childpid==0)
   {
     if(execlp("xterm","xterm","-e",child,inet_ntoa(ip),strpfd,NULL)==-1 )
     {
       err_sys("!!execlp error in child.Abort.\n");
     }
   }
   
   // Back in parent
   int n;
   printf("#Waiting for child status\n");
   close(pfd[1]);
   
   FD_ZERO(&rset);
   for(;;)
   {
    char ignore[MAXLINE]; // If user interact with parent window when child window is active, flush parent's stdin.
    
    FD_SET(fileno(stdin),&rset);
    FD_SET(pfd[0],&rset);

    maxfdp1 = max(fileno(stdin),pfd[0] ) + 1;
    if( select(maxfdp1,&rset,NULL,NULL,NULL)<0 )
    {
     if (errno == EINTR)
       continue;
     else
      err_sys("select error");
    }

    if( FD_ISSET(fileno(stdin), &rset) )
    {
     Fgets(ignore, MAXLINE, stdin);
     printf("!!You are interacting with wrong window. Look for another window.\n");
    }
    if( FD_ISSET(pfd[0],&rset)  )
    {
     n=Readline(pfd[0],childstatus,MAXLINE);
     if(n==0)
     {
       printf("!!child closed pipe.\n");
       close(pfd[0]);
       return;
     }
     childstatus[n]='\0';
     printf("!!%s\n",childstatus);
    }
  }
}

void
sig_cleanup(int signo)
{
  if( childpid!=NULL )
    {
      printf("!!Terminating child.\n");
      sigsend(P_PID,childpid,SIGKILL);
    }
  printf("!!Exiting.GoodBye.\n");
  exit(0);
}
void
sig_chld(int signo)
{
        pid_t   pid;
        int             stat;

        while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                printf("child %ld terminated\n", pid);
                childpid=NULL;
        }
        return;
}

