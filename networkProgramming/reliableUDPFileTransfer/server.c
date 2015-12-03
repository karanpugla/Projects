#include"unpifiplus.h"
#include<stdio.h>
#include<libgen.h>
#include <sys/stat.h>
#include"myhdr.h"
#include"unprttplus.h"

#define MAXCONN 5
#define SUCCESS 1
#define LOCK 2
#define TIMEOUT 3
#define INTERFACES 4

typedef struct 
{
  int sockfd;
  struct ifi_info *ifi;
  struct in_addr  ifi_subnet;
}ifisockfdsubnet;


typedef struct 
{
  uint32_t cli_IP;
  uint32_t cli_PORT;
  uint32_t child_PID;
  uint32_t valid;
}udpconnections;

struct wndlist_t
{
  FTmsg *DG;
  struct wndlist_t *next;
};

int timeout=0;
char *logID;
static struct rtt_info rttinfo;

udpconnections connections[MAXCONN];
ifisockfdsubnet udpsockets[3];
int ifcount=0;

int cmp_by_ntmlen(const void *a, const void *b);
void sig_chld(int signo);
void handlerequest(int index, struct sockaddr_in cliaddr,FTmsg *msg);
void handletimeout(int sockfd, struct wndlist_t *wndhead,struct wndlist_t *wndend);
void sig_alrm(int signo);
void NOTIFY(int note);


int main(int argc, char *argv[])
{
  int sockfd,i,j;
  const int on;
  struct ifi_info *ifihead, *ifi;
  struct sockaddr_in *sa, *netmask;
  ifihead=Get_ifi_info_plus(AF_INET, 1);
  FILE *fp;
  char opt[10];
  int port;
  fd_set rset;
  int maxfdp1=0;
  int freeslot=-1;
  struct sockaddr_in cliaddr;
  socklen_t clilen=sizeof( struct sockaddr_in);
  FTmsg msg;
  int retranDG,pid;

  if( !(fp=fopen("server.in","r")) )
  {
    printf("error: cannot open file %s\n", argv[1]);
    exit(1);
  }

  if( ! fgets(opt,10,fp) )
  {
    printf("error: cannot read port number from file\n");
    exit(1);
  }
  
  if(opt[strlen(opt)-1]=='\n')
     opt[strlen(opt)-1]='\0';

// Bind a socket on each interface.
  port=atoi(opt);
  NOTIFY(INTERFACES);
  for(i=0,ifi=ifihead ;ifi!=NULL ;ifcount++,ifi=ifi->ifi_next)
  {
    sockfd=Socket(AF_INET, SOCK_DGRAM, 0);
    Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sa= (struct sockaddr_in *) ifi->ifi_addr;
    sa->sin_family=AF_INET;
    sa->sin_port=htons(port);
    Bind(sockfd, (SA *) sa, sizeof(*sa));
    
    netmask=(struct sockaddr_in *)ifi->ifi_ntmaddr;
    
    udpsockets[ifcount].sockfd=sockfd;
    udpsockets[ifcount].ifi=ifi;
    udpsockets[ifcount].ifi_subnet.s_addr= sa->sin_addr.s_addr & netmask->sin_addr.s_addr;
    
    printf( "UDP socket %d on if=%s IP=%s ",udpsockets[ifcount].sockfd, udpsockets[ifcount].ifi->ifi_name, inet_ntoa(sa->sin_addr));
    printf( "netmask=%s ",inet_ntoa(netmask->sin_addr) );
    printf( "subnet=%s is bound to port %d\n", inet_ntoa(udpsockets[ifcount].ifi_subnet),port );
  }

// Sort array of sockets in decreasing order of netmask length. Need this to perform longest prefix match later
 qsort(udpsockets,ifcount,sizeof(ifisockfdsubnet),cmp_by_ntmlen);
 
// for(i=0;i<ifcount;i++)
// {
//  netmask=(struct sockaddr_in *)udpsockets[i].ifi->ifi_addr;
//  printf("%s\n",inet_ntoa(netmask->sin_addr));
// }
  
// When child is killed, free the client connection(IP,PORT) handled by the child.
  Signal(SIGCHLD, sig_chld);

  FD_ZERO(&rset);
  for( ; ; )
  {
    for(i=0;i<ifcount;i++)
    {
      FD_SET(udpsockets[i].sockfd,&rset);
      maxfdp1=max(udpsockets[i].sockfd,maxfdp1);
    }
    maxfdp1=maxfdp1+1;
    
    printf("waiting on select\n");
    if( select(maxfdp1, &rset, NULL, NULL, NULL) < 0 )
    {
    // Take care if parent gets interrupted.
     if (errno == EINTR)
       continue;
     else
       err_sys("select error");
    }

    for(i=0;i<ifcount;i++)
    {
      retranDG=0;
      if (FD_ISSET(udpsockets[i].sockfd, &rset))
      {
        if( recvfrom(udpsockets[i].sockfd,&msg, 512, NULL, (struct sockaddr *)&cliaddr, &clilen)<0 )
        {
          printf("error: cannot recvfrom socket.\n");
          exit(1);
        }
        sa=(struct sockaddr_in *) udpsockets[i].ifi->ifi_addr;
        printf("Received new connection on %s from %s:%d\n"\
                ,inet_ntoa( sa->sin_addr)\
                ,inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port) );

        for(j=0;j<MAXCONN;j++)
        {

          if( connections[j].cli_IP==cliaddr.sin_addr.s_addr &&\
              connections[j].cli_PORT==cliaddr.sin_port &&\
              connections[j].valid==1 )
          {
            printf("Client retransmission of connection. Ignore\n");
            retranDG=1;
            break;
          }
        }
        
        if(retranDG)
          continue;
       
        for(j=0;j<MAXCONN;j++)
        {
          if( connections[j].valid!=1 )
          {
            freeslot=j;
            //printf("Found freeslot=%d\n",freeslot);
            break;
          }
        }
        connections[freeslot].cli_IP=cliaddr.sin_addr.s_addr;
        connections[freeslot].cli_PORT=cliaddr.sin_port;
        connections[freeslot].valid=1;
        
        if( (pid=fork()) == 0 )
          handlerequest(i,cliaddr, &msg);
      
        printf("Fork pid=%d\n",pid);
        connections[freeslot].child_PID=pid;
        //printf("-fork()\n");
      }
    }
  }
return 0;
}

void handlerequest(int index, struct sockaddr_in cliaddr, FTmsg *msg)
{
  
  int i,n,fn, val,islocal=0, printonce=1,FTcomplete=0;
  struct sockaddr_in *sa_in;
  struct sockaddr_in newsockaddr;
  int sockfd,on;
  int sockportretrancount=0,sockportretrantmo=2,newsocklen;
  char newportACK[4];
  char * filename;
  FILE *fp;
  struct stat filestat;
  int maxseqno,wndsize,wndfreespace,seqno;
  Sigfunc *sigfunc;
  FTmsg *DG, cliDG;
  struct wndlist_t *wndhead=NULL,*wndend=NULL,*wndslot=NULL, *tmp;
  
  
  rtt_d_flag=1;
  ntoFTmsg(msg);
  filename=msg->payload;
  logID=basename(filename);
  printf("%s: Handle request filename=%s\n",logID,filename);
  
  // Close all sockets except the one at index
  // Also determine if cliaddr is local

  for(i=0;i<ifcount;i++)
  {
    if(i!=index)
      Close(udpsockets[i].sockfd);
   
    sa_in=(struct sockaddr_in *) udpsockets[i].ifi->ifi_ntmaddr;
    if( udpsockets[i].ifi_subnet.s_addr == (cliaddr.sin_addr.s_addr & sa_in->sin_addr.s_addr ) )
      islocal=1;
  }

    sockfd=Socket(AF_INET, SOCK_DGRAM, 0);
    if(islocal)
    {
      printf("%s: Client is local set SO_DONTROUTE.\n",logID);
      Setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on));
    }
    else 
      printf("%s: Client is not local.\n",logID);
    
    sa_in=(struct sockaddr_in *) udpsockets[index].ifi->ifi_addr;
    
    newsockaddr.sin_family=AF_INET;
    newsockaddr.sin_addr=sa_in->sin_addr;
    newsockaddr.sin_port=htons(0);
    Bind(sockfd,(SA *) &newsockaddr, sizeof(newsockaddr));
    
    newsocklen=sizeof(newsockaddr);
    Getsockname(sockfd, (SA *) &newsockaddr, &newsocklen);
    printf("%s: New port=%d\n",logID,ntohs(newsockaddr.sin_port) );
    Connect(sockfd, (SA *) &cliaddr,sizeof(cliaddr));
    
  
  if( !(fp=fopen(filename,"r")) )
  {
    printf("%s: cannot open file %s requested by client.Abort\n",logID, filename);
    msg->ts=-1;
    Sendto(udpsockets[index].sockfd,msg,sizeof(FTmsg), NULL,\
            (SA*) &cliaddr, sizeof(cliaddr) );
    exit(1);
  }
  
  stat(filename,&filestat);
  maxseqno = (filestat.st_size%500)==0?filestat.st_size/500:filestat.st_size/500+1;
  wndsize=wndfreespace=msg->wnd;
  
  msg->seqno=ntohs(newsockaddr.sin_port);  
  msg->ts=maxseqno;
  FTmsgton(msg);
sendagain: 
    Sendto(udpsockets[index].sockfd,msg,sizeof(FTmsg), NULL,\
            (SA*) &cliaddr, sizeof(cliaddr) );
    printf("%s: Waiting %d sec for ACK to new port no.\n", logID,sockportretrantmo);
    if (Readable_timeo(sockfd, sockportretrantmo) == 0) 
    {
      if(sockportretrancount <=2)
      {
        sockportretrancount++;
        printf("%s: sending new port no. timeout, retransmitting attempt %d\n",logID, sockportretrancount);
        sockportretrantmo+=1;
        Writen(sockfd,msg, sizeof(FTmsg));
        goto sendagain;
      }
      else
      {
        printf("%s: sending new port no. failed. Abort\n",logID);
        exit(1);
      }
    }
    else
    {
      if( read(sockfd,newportACK,sizeof(newportACK))<0 && sockportretrancount ==0 )
      {
        printf("%s: sending new port no. failed. Abort\n",logID);
        exit(1);
      }
    }

  printf("%s: ACK to new port no. received: %s. Closing parent listening socket.\n",logID,newportACK);
  Close(udpsockets[index].sockfd);


  
  sigfunc=Signal(SIGALRM,sig_alrm);

  val=Fcntl(sockfd, F_GETFL,0);
  Fcntl(sockfd, F_SETFL,val|O_NONBLOCK);
  seqno=maxseqno;
  rtt_init(&rttinfo);
//  rtt_debug(&rttinfo);
  while(seqno!=0)
  {
    if(timeout)
    {
      timeout=0;
      handletimeout(sockfd,wndhead,wndend);
    }
    n=fn=0;
    if(wndfreespace!=0)
    {
//      printf("%s: wndfreespace=%d\n",logID,wndfreespace);
      DG=malloc(sizeof(FTmsg));
      DG->seqno=seqno;
      seqno--;
      fn=Readn(fileno(fp),&DG->payload,500);
      if(fn<500)
      DG->payload[fn]='\0';
      wndslot=malloc(sizeof(struct wndlist_t));
      wndslot->DG=DG; wndslot->next=NULL;
      if(wndend==NULL)
      {
        wndhead=wndend=wndslot;
      // Putting first DG in window. So start timer for this DG.
        rtt_newpack(&rttinfo);
        rtt_debug(&rttinfo);
        ualarm(rtt_start(&rttinfo),0);
        printf("%s: Setting timeout=%u usec for DG=%d  in window\n",logID,rtt_start(&rttinfo),DG->seqno);
      }
      else
      {
        wndend->next=wndslot;
        wndend=wndslot;
      }
      wndfreespace--;
      printf("%s: Sending seqno=%d\n",logID,DG->seqno);
//      printf("%s: wndhead.seqno=%d, wndendseqno=%d\n",logID,wndhead->DG->seqno,wndend->DG->seqno);
      DG->ts = rtt_ts(&rttinfo);
//      printf("%s: TS for seqno=%d is %u usec\n",logID,wndhead->DG->seqno,wndend->DG->ts);
      Write(sockfd,FTmsgton(DG), sizeof(FTmsg));
    }
    
    do
    {
      if(timeout)
      {
        timeout=0;
        handletimeout(sockfd,wndhead,wndend);
      }
      if(wndfreespace==0 && printonce)
      {
        printf("%s: Sender window LOCK. ",logID);
        printf("Wating for ACK to break lock\n");
        NOTIFY(LOCK);
        printonce=0;
      }

      if(seqno==0 && FTcomplete==0)
      {
        printf("%s: File transfer complete. Waiting for last DG to be ACKed.\n",logID);
        FTcomplete=1;
      }
      if( (n=read(sockfd,&cliDG,sizeof(FTmsg))) < 0 )
      {
        if( errno != EWOULDBLOCK)
        {
          printf("%s: read error on socket.Abort\n",logID);
          exit(1);
        }
      }
      else 
      {
        ntoFTmsg(&cliDG);
        printf("%s: Received ACK for seqno=%d\n",logID,cliDG.seqno);
        if(cliDG.seqno < wndend->DG->seqno)
        {
          printf("%s: Received ACK for seqno=%d cross end of window, most recent  seqno %d in window \n",logID,cliDG.seqno, wndend->DG->seqno);
        }
        else if(cliDG.seqno > wndhead->DG->seqno)
        {
          printf("%s: Received ACK for seqno=%d cross start of window,  oldest seqno %d in window\n",logID,cliDG.seqno,wndhead->DG->seqno);
        }
        else
        {
          ualarm(0,0);
          for(i=wndhead->DG->seqno;i>=cliDG.seqno;i--)
          {
            if(wndhead->DG->seqno==cliDG.seqno)
            {
              rtt_stop(&rttinfo, (rtt_ts(&rttinfo) - ntohl(wndhead->DG->ts))* 1000 );
              rtt_debug(&rttinfo);
//              printf("%s: rtt_stop=%u usec received ack for seqno=%d\n",logID,rtt_ts(&rttinfo) - ntohl(wndhead->DG->ts),cliDG.seqno);
            }
            printf("%s: Freeing DG seqno=%d\n",logID,wndhead->DG->seqno);
            free(wndhead->DG);
            tmp=wndhead->next;
            free(wndhead);
            wndhead=tmp;
            wndfreespace++;
          }
          printonce=1;
          if(wndhead==NULL)
          {
            wndend=NULL;
          }
          else
          {
            printf("%s: Setting timeout to %d usec\n",logID,rtt_start(&rttinfo));
            ualarm(rtt_start(&rttinfo),0);
            rtt_newpack(&rttinfo);
            rtt_debug(&rttinfo);
          }
          if(cliDG.seqno==1)
          {
            printf("%s: All DGs transmitted successfully.\n",logID);
            fflush(stdout);
            NOTIFY(SUCCESS);
            break;
          }

        }
      }
    }while(wndfreespace==0 || FTcomplete);
  }

  exit(0);
}

void handletimeout(int sockfd, struct wndlist_t *wndhead, struct wndlist_t *wndend)
{
  int i;
  struct wndlist_t *ptr;
 
  ualarm(0,0);
  NOTIFY(TIMEOUT);
  printf("%s: Handle TIMEOUT.\n",logID);
  
  if(wndhead==NULL)
  {
    printf("%s: Window empty, received ACK just after timeout. Skip retransmitting.\n",logID);
    return;
  }

  if( rtt_timeout(&rttinfo)== -1 )
  {
    printf("%s: Max retransmission  of seqno=%d exceeded. Network is very lossy. Abort\n",logID,ntohl(wndhead->DG->seqno));
    exit(1);
  }

  rtt_debug(&rttinfo);
  ptr=wndhead;
  for(i=wndhead->DG->seqno;i>=wndend->DG->seqno;i--)
  {
    Write(sockfd,ptr->DG,sizeof(FTmsg));
    printf("%s: Retransmitted seqno=%d.\n",logID,ntohl(ptr->DG->seqno));
    fflush(stdout);
    ptr=ptr->next;
  }
  ualarm(rtt_start(&rttinfo),0);
  printf("%s: Setting timeout to %d usec\n",logID,rtt_start(&rttinfo));
}

void sig_alrm(int signo)
{
  printf("%s: TIMEOUT.\n",logID);
  timeout=1;
}


int cmp_by_ntmlen(const void *a, const void *b)
{
  ifisockfdsubnet *ua= (ifisockfdsubnet *)a;
  ifisockfdsubnet *ub= (ifisockfdsubnet *)b;
  
  if(ub->ifi_subnet.s_addr == ua->ifi_subnet.s_addr)
    return 0;
  else 
    return  ub->ifi_subnet.s_addr > ua->ifi_subnet.s_addr ? 1 : -1;

}
void
sig_chld(int signo)
{
        pid_t   pid;
        int     stat,i;

        while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                printf("child %ld terminated\n", pid);
                for(i=0;i<MAXCONN;i++)
                {
                  if(connections[i].child_PID==pid)
                  {
                    connections[i].valid=-1;
                    printf("Making connections[%d] -1\n",i);
                    break;
                  }
                }
        }
        return;
}
void NOTIFY(int note)
{
FILE *fp;
char *filename;
int n;
char buf[2048];


switch(note)
{
case SUCCESS:
      filename="success.in";
      break;;
case LOCK:
      filename="lock.in";
      break;;
case TIMEOUT:
      filename="timeout.in";
      break;;
case INTERFACES:
      filename="interfaces.in";
      break;;
}
if( !(fp=fopen(filename,"r")) )
  return;


n=read(fileno(fp),buf,2048);
buf[n]='\0';
printf("%s",buf);
fflush(stdout);
}
