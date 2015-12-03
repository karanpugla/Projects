#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <limits.h>
#include <netinet/in.h>
#include "unpifiplus.h"
#include <string.h>
#include "myhdr.h"

#define PORTLEN 10

extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
extern        void      free_ifi_info_plus(struct ifi_info *ifihead);

pthread_mutex_t lock;
pthread_cond_t cond;

// Converts ip string to integer
unsigned long ip_to_int(char *ip)
{
    int a, b, c, d;

    sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
    return d | c << 8 | b << 16 | a << 24;
}

// Sorts the interfaces in decreasing order of netmasks
int cmp_by_mask(const void *a, const void *b)
{
    struct ifi_info *if1, *if2;
    struct sockaddr *sa1, *sa2;
    if1 = *(struct ifi_info **)a;
    if2 = *(struct ifi_info **)b;
    sa1 = if1->ifi_ntmaddr;
    sa2 = if2->ifi_ntmaddr;
    int rc=0;

    if (sa1 != NULL || sa2 != NULL) {
        rc = ip_to_int(Sock_ntop_host(sa1, sizeof(*sa1))) >
             ip_to_int(Sock_ntop_host(sa2, sizeof(*sa2))) ? -1 : 1;
    }

    return rc;
}

// Checks if the server on local host, subnet or elsewhere
int check_server_local(struct ifi_info *ifihead, char *ipbf,
                       struct ifi_info *ifi_cli)
{
    struct ifi_info *ifi, *if_array[10];
    struct sockaddr *sa, *sa1;
    unsigned long ip, mask, cip;
    int flag = 0, rc, ifcount=0, i;

    cip = ip_to_int(ipbf);

    /* check if server on same host.
     * Counts the number of interfaces too which is required in case
     * the server not on local host.
     */
    for (ifi = ifihead; ifi != NULL; ifi = ifi->ifi_next) {
        if ( (sa = ifi->ifi_addr) != NULL) {
            rc = strcmp(ipbf, Sock_ntop_host(sa, sizeof(*sa)));
            if (!rc) {
                flag = 1;
                break;
            }
        }
        ifcount++;
    }

    if (flag) {
        //get loopback interface
        for (ifi = ifihead; ifi != NULL; ifi = ifi->ifi_next) {
            if ( (sa = ifi->ifi_addr) != NULL) {
                rc = strcmp("127.0.0.1", Sock_ntop_host(sa, sizeof(*sa)));
                if (!rc) {
                    (*ifi_cli).ifi_addr = ifi->ifi_addr;
                    (*ifi_cli).ifi_ntmaddr = ifi->ifi_ntmaddr;
                    break;
                }
            }
        }
        return 0;
    }

    /* check if server on same subnet.
     * You would have to sort it first in decreasing order of masks
     * and apply one by one. The one that matches would be the longest
     * mask matching.
     * We do the sorting using qsort which requires the interfaces to
     * be stored in an array.
     */
    for (ifi = ifihead, i=0; ifi != NULL; ifi = ifi->ifi_next, i++)
        if_array[i] = ifi;

    qsort(if_array, ifcount, sizeof(struct ifi_info *), cmp_by_mask);

    for (i=0; i<ifcount; i++) {
        sa = if_array[i]->ifi_addr;
        sa1 = if_array[i]->ifi_ntmaddr;
        if (sa != NULL && sa1 != NULL) {
            ip = ip_to_int( Sock_ntop_host(sa, sizeof(*sa)));
            mask = ip_to_int( Sock_ntop_host(sa1, sizeof(*sa1)));
            if ((ip & mask) == (cip & mask)) {
                flag = 1;
                (*ifi_cli).ifi_addr = sa;
                (*ifi_cli).ifi_ntmaddr = sa1;
                break;
            }
        }
    }

    if (flag)
        return 1;

    return 2;
}

void print_subnet(char *ip, char *mask)
{
    int a, b, c, d, e, f, g, h;
    sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
    sscanf(mask, "%d.%d.%d.%d", &e, &f, &g, &h);
    printf("  Subnet: %d.%d.%d.%d\n", a & e, b & f, c & g, d & h);

}

// Prints the client interfaces
void print_interfaces(struct ifi_info *ifihead)
{
    struct ifi_info *ifi;
    struct sockaddr *sa, *sa1;
    char *ip = NULL, *mask = NULL;

    printf("Client interfaces:\n");
    for (ifi = ifihead; ifi != NULL; ifi = ifi->ifi_next) {
        printf("%s: \n", ifi->ifi_name);
        if (ifi->ifi_index != 0)
            printf("(%d) ", ifi->ifi_index);
        if ( (sa = ifi->ifi_addr) != NULL) {
            ip = (char *)malloc(INET_ADDRSTRLEN);
            strcpy(ip, Sock_ntop_host(sa, sizeof(*sa)));
            printf("  IP addr: %s\n", ip);
        }
        if ( (sa1 = ifi->ifi_ntmaddr) != NULL) {
            mask = (char *)malloc(INET_ADDRSTRLEN);
            strcpy(mask, Sock_ntop_host(sa1, sizeof(*sa1)));
            printf("  network mask: %s\n", mask);
        }
        print_subnet(ip, mask);
    }
    free_ifi_info_plus(ifihead);
}

// reads the client.in file for initialization
int read_client_in(char *inf, struct client_in *args)
{
    char *ipbuf, *filename;
    FILE *f;
    int rc;

    ipbuf = (char *)malloc(INET_ADDRSTRLEN);
    filename = (char *)malloc(PATH_MAX);

    //Reading from client.in
    f = fopen("client.in", "r");
    if (!f)
        return 1;

    rc = fscanf(f, "%s", ipbuf);
    if (rc == EOF)
        return 1;
    args->server_ip = ipbuf;

    rc = fscanf(f, "%d", &(args->server_port));
    if (rc == EOF)
        return 1;

    rc = fscanf(f, "%s", filename);
    if (rc == EOF)
        return 1;
    args->filename = filename;

    rc = fscanf(f, "%d", &(args->r_wnd));
    if (rc == EOF)
        return 1;

    rc = fscanf(f, "%d", &args->seed);
    if (rc == EOF)
        return 1;

    rc = fscanf(f, "%f", &args->p);
    if (rc == EOF)
        return 1;

    rc = fscanf(f, "%d", &args->mu);
    if (rc == EOF)
        return 1;

    return 0;
}

//Prints the values read from client.in
void print_creds(struct client_in *args)
{
    printf("SERVER IP : %s\n", args->server_ip);
    printf("SERVER PORT : %d\n", args->server_port);
    printf("FILENAME : %s\n", args->filename);
    printf("RECEIVER WINDOW : %d\n", args->r_wnd);
    printf("SEED : %d\n", args->seed);
    printf("PROBABILITY : %f\n", args->p);
    printf("MU : %d\n\n", args->mu);
}

int get_sleep_time(int mu)
{
    return (-1 * mu * log10( rand () % 10 / (float)10));
}

void read_buffer(void *Buf)
{
    struct buffer *B = (struct buffer *)Buf;
    FTmsg *msg;
    char tempbf[500];
    int st;

    printf("Printing datagrams\n");
    //pthread_mutex_lock(&lock);
    //pthread_cond_wait(&cond, &lock);
    if (!B->writefrm) {
        printf("No datagram found\n");
        return;
    }

print:
    while (B->buf[B->writefrm] != NULL) {
        msg = B->buf[B->writefrm];
        strcpy(tempbf, msg->payload);
        tempbf[500] = 0;
        printf("%s", tempbf);
        fflush(stdout);
        B->writefrm--;
    }
    //pthread_mutex_unlock(&lock);
    if (!B->writefrm)
        printf("Printed till datagram %d\n", B->writefrm);

    //check for sleep time
    if (!B->buf[B->writefrm]) {
        st = get_sleep_time(B->mu);
        if (st > 20)
            st = 5;
        printf("Read thread sleeping for %d s\n", st);
        sleep(st);
        if (!B->writefrm)
            printf("Printed all datagrams\n");
        else
            goto print;
    }
    return;
}

ssize_t read_with_p(int sockfd, void *msg, int len, float p)
{
    int n, accept;

    n = Read(sockfd, msg, len);
    if (n == -1)
        printf("Read error\n");
    float x = (rand() % 10 ) / (float)10;
    accept = (x > p) ? 1 : 0;
    if (!accept) {
        printf("Lost datagram %d in network; p = %f\n", ((FTmsg *)msg)->seqno, x);
        return -1;
    }
    else {
        return n;
    }
}

ssize_t myread(int sockfd, void *msg, int len, float p)
{
    int n;
    do {
        n = read_with_p(sockfd, msg, len, p);
    } while (n == -1);
    return n;
}

int send_ack_with_p(int sockfd, void *msg, int len, float p)
{
    int send_ack;

    float x = (rand() % 10 ) / (float)10;
    //printf("prob=%f;", x);
    send_ack = x >= p ? 1 : 0;
    if (send_ack) {
        Write(sockfd, msg, len);
        return 1;
    } else {
        return 0;
    }
}

void read_file(int sockfd, struct buffer *B, float p)
{
    FTmsg *msg=NULL, ackmsg;
    int expected, allowed;
    int rc;
    pthread_t tid;

    //Read the first datagram
    printf("Waiting for first datagram\n");
    msg = (FTmsg *)malloc(sizeof(FTmsg));
    myread(sockfd, msg, sizeof(FTmsg), p);
    printf("Received first datagram %d. ", msg->seqno);

    B->writefrm = msg->seqno;
    B->buf[msg->seqno] = msg;
    expected = msg->seqno - 1;
    printf("Expecting datagram %d\n", expected);
    printf("-------------------------------------------------------------------\n");

    /*if (pthread_mutex_init(&lock, NULL) != 0)
        printf("\n mutex init failed\n");
    if (pthread_cond_init(&cond, NULL) != 0)
        printf("condition variable init failed\n"); */

    //Creating a thread to consume the datagrams
    printf("Spawning a Reader thread for printing the datagrams\n");
    printf("-------------------------------------------------------------------\n");
    Pthread_create(&tid, NULL, &read_buffer, (void *)B);

    //Read the rest of the datagrams
    do {
        ackmsg.seqno = expected + 1;
        allowed = B->writefrm - B->wnd + 1;
        //ackmsg.wnd = expected - allowed + 1;
        rc = send_ack_with_p(sockfd, &ackmsg, sizeof(FTmsg), p);
        if (rc) {
            printf("Sent an ACK for datagram %d\n", ackmsg.seqno);
            fflush(stdout);
        } else {
            printf("Sent ACK lost in network\n");
            fflush(stdout);
        }

        msg = (FTmsg *)malloc(sizeof(FTmsg));
        myread(sockfd, msg, sizeof(FTmsg), p);
        printf("Received datagram %d\n", msg->seqno);
        fflush(stdout);

        //check if the datagram is expected one
        if (msg->seqno != expected) {
            printf("Discarded datagram %d, expecting %d\n", msg->seqno, expected);
            fflush(stdout);
            continue;
        }

        //pthread_mutex_lock(&lock);
        B->buf[expected] = msg;
        //printf("wrote %d datagram\n", B->writefrm);
        fflush(stdout);
        //pthread_mutex_unlock(&lock);

        //check if the window is full
        if (expected > allowed) {
            expected -= 1;
        } else {
            printf("Window got full.\n");
            fflush(stdout);
            //pthread_cond_signal(&cond);
            allowed = B->writefrm - B->wnd + 1;
            //printf("Allowed till %d\n", allowed);
            //printf("Expecting %d\n", expected);
            for (; expected <= allowed;) {
                printf("Blocking till Reader thread prints some datagrams\n");
                ackmsg.seqno = expected;
                rc = send_ack_with_p(sockfd, &ackmsg, sizeof(FTmsg), p);
                if (rc) {
                    printf("Sent an ACK for datagram %d\n", expected);
                    fflush(stdout);
                } else {
                    printf("Sent ACK lost in network\n");
                    fflush(stdout);
                }
                sleep(1);
                allowed = B->writefrm - B->wnd + 1;
                //printf("Allowed till %d\n", allowed);
            }
            expected--;
        }
        printf("-------------------------------------------------------------------\n");

    } while (expected);

    //Recevied all datagrams, wait for thread to consume all of them
    printf("Received all datagrams. Waiting for Reader thread to print the left datagrams\n");
    printf("-------------------------------------------------------------------\n");
    fflush(stdout);

    ackmsg.seqno = expected + 1;
    send_ack_with_p(sockfd, &ackmsg, sizeof(FTmsg), p);
    printf("Entering into TIME_WAIT state for 6 seconds\n");
    fflush(stdout);
    if (Readable_timeo(sockfd, 6) != 0) {
        Read(sockfd, msg, sizeof(FTmsg));
        printf("Received seq no %d while in TIME_WAIT state. Sending an ACK one last time\n", msg->seqno);
        fflush(stdout);
        Write(sockfd, &ackmsg, sizeof(FTmsg));
    }
    printf("-------------------------------------------------------------------\n");
    printf("File transfer complete. Closing connection\n");
    fflush(stdout);

    // pthread join
    pthread_join(tid, NULL);
    printf("Read thread destroyed\n");

    //Destroying mutex
    //pthread_mutex_destroy(&lock);

    free(B->buf);
    free(B);
    return;
}

int main(int argc, char *argv[])
{
    int rc = 0;

    char ipcli[INET_ADDRSTRLEN];
    struct client_in args;
    char buf[1000];

    struct sockaddr_in server, new_s;
    struct sockaddr *sa = NULL;

    int family=AF_INET, doaliases=0;
    struct ifi_info *ifihead, ifi_cli;

    int sockfd ;
    struct sockaddr_in *ca, ca1;
    struct sockaddr_in post_ca, post_sa;
    int ca_len, buflen;
    FTmsg fpkt, *msg;


    struct buffer *B;

    //read the client.in
    rc = read_client_in("client.in", &args);
    if (rc) {
        printf("error reading client.in file\n");
        goto out;
    }

    printf("-------------------------------------------------------------------\n");
    print_creds(&args);
    printf("-------------------------------------------------------------------\n");

    //Initializing random seed
    srand(args.seed);

    //Determine client interfaces
    ifihead = Get_ifi_info_plus(family, doaliases);
    print_interfaces(ifihead);

    //Check server locality
    printf("Checking location of server %s\n", args.server_ip);
    rc = check_server_local(ifihead, args.server_ip, &ifi_cli);
    if (rc == 0) {
        printf("Server on local host\n");
        strcpy(args.server_ip, "127.0.0.1");
        sa = ifi_cli.ifi_addr;
        strcpy(ipcli, Sock_ntop_host(sa, sizeof(*sa)));
        printf("client %s connecting to server %s\n", ipcli, args.server_ip);
    } else if (rc == 1) {
        printf("Server on same subnet\n");
        sa = ifi_cli.ifi_addr;
        strcpy(ipcli, Sock_ntop_host(sa, sizeof(*sa)));
        printf("client %s connecting to server %s\n", ipcli, args.server_ip);
    } else {
        printf("Server on different subnet\n");
    }
    printf("-------------------------------------------------------------------\n");

    /*Client creates a socket and binds its ip address and assigns a
     * temporary port. It then connects() to server.
     */
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    if ( (rc==0) || (rc==1) ) {
        ca = (struct sockaddr_in *)sa;
    } else {
        ca1.sin_addr.s_addr = INADDR_ANY;
        ca = &ca1;
    }
    ca->sin_port = htons(0);
    ca->sin_family = AF_INET;
    Bind(sockfd, (SA *)ca, sizeof(*ca));

    // Verify client credentials after bind
    ca_len = sizeof(post_ca);
    Getsockname(sockfd, (SA *)&post_ca, &ca_len);
    printf("Client ip : %s; port : %d\n",
            Sock_ntop((SA *)&post_ca, ca_len), ntohs(post_ca.sin_port));

    // connects to server
    server.sin_family = AF_INET;
    server.sin_port = htons(args.server_port);
    Inet_pton(AF_INET,args.server_ip, &(server.sin_addr));
    Connect(sockfd, (SA *)&server, sizeof(server));

    // Verify server credentials after connect
    Getpeername(sockfd, (SA *)&post_sa, &ca_len);
    printf("Server ip : %s; port : %d\n",
            Sock_ntop((SA *)&post_sa, ca_len), ntohs(post_sa.sin_port));

    //Send filename to server
    strcpy(fpkt.payload, args.filename);
    fpkt.wnd = args.r_wnd;
    Write(sockfd, &fpkt, sizeof(FTmsg));
    printf("-------------------------------------------------------------------\n");
    printf("Sent filename : %s\n", args.filename);
    printf("-------------------------------------------------------------------\n");

    //Receive the new port number from server child
    msg = (FTmsg *)malloc(sizeof(FTmsg));
    Read(sockfd, msg, sizeof(FTmsg));
    buflen = msg->ts;
    //printf("Total num of DGs = %d\n", buflen);
    if ((int)msg->ts == -1) {
        printf("Server: File %s doesnt exist\n", args.filename);
        exit(1);
    }
    new_s.sin_port = htons(msg->seqno);
    printf("New port received : %d\n", ntohs(new_s.sin_port));
    printf("-------------------------------------------------------------------\n");

    //Initialize buffers
    B = (struct buffer *)malloc(sizeof(struct buffer));
    B->buf = (FTmsg **) malloc(sizeof(FTmsg *) * (buflen+1));
    B->mu = args.mu;
    B->wnd = args.r_wnd;

    //Connect to new socket
    new_s.sin_family = AF_INET;
    Inet_pton(AF_INET,args.server_ip, &(new_s.sin_addr));
    Connect(sockfd, (SA *)&new_s, sizeof(new_s));

    Getpeername(sockfd, (SA *)&post_sa, &ca_len);
    printf("Server ip : %s; port : %d\n",
            Sock_ntop((SA *)&post_sa, ca_len), ntohs(post_sa.sin_port));

    // Send "ACK" to the server child
    bzero(buf, 1000);
    strcpy(buf, "ACK");
    printf("Sending ack\n");
    Write(sockfd, buf, strlen(buf) + 1);
    printf("Handshake completed\n");
    printf("-------------------------------------------------------------------\n");

    //Read the file datagrams into buffer
    read_file(sockfd, B, args.p);
    printf("-------------------------------------------------------------------\n");

    //close the socket
    close(sockfd);

out:
    free(args.server_ip);
    free(args.filename);
    exit(rc);
}
