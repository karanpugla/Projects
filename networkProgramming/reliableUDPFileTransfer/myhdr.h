typedef struct
{
  uint32_t seqno;
  uint32_t ts;
  uint32_t wnd;
  char payload[500];
}FTmsg;

FTmsg* FTmsgton(FTmsg *DG);
FTmsg* ntoFTmsg(FTmsg *DG);

struct client_in {
    char *server_ip;
    int server_port;
    char *filename;
    int r_wnd; //Receiver window size (in num of datagrams)
    int seed;
    float p;  // probability of packet loss. simulates congestion
            // p = 0 => no congestion; p=1 => full congestion
    int mu; //mean time for client thread to read the buffer
};

struct buffer {
    int writefrm;
    int wnd;
    int mu;
    FTmsg **buf;
};
