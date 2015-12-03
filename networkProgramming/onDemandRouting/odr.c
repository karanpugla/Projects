#include "unp.h"
#include "hw_addrs.h"
#include "./common.h"
#include "linux/if_ether.h"
#include "netpacket/packet.h"

const int ETH_P_ODR = 0x2661;
const int OLD_PORT_TIMER = 10; // in seconds, if no activity on this port make it available for re-use
#define MAX_PORTS  1024
#define MAX_ROUTES 20
#define MAX_RECVDRREQS_LOG 200
#define MAX_CLIENT_MSG_BUF 100
#define ODR_ETH_MSG_SIZE (sizeof(struct ethhdr) + sizeof(struct ODR_RREX_t))
const int SERVER_PORT = 0;
#define RREQ  0
#define RREP  1
#define APPL  2
#define printf(...) printf(__VA_ARGS__); fflush(stdout);

struct routing_table_entry{
	int valid; // 0 means invalid, 1 means valid
	in_addr_t dest;
	char nexthop[ETH_ALEN];
	int ifindex;
	int hopcount;
	struct timeval time;
};
struct ephemeral_port{
	int busy;
	struct timeval time;
	char path[1024];
};
struct ODR_RREX_t{ //Can be RREQ or RREP or APPL
	int type;
	int hopcount;
	in_addr_t src;
	in_addr_t dest;
	int sport;
	int dport;
	int broadcastid;
	int size;
	char data[64];
	int repsent;
	int forceRedisc;
};

struct recvdRREQLog_t{
	int valid;
	in_addr_t src;
	int broadcastid;
	int hopcount;
};

struct clientMsgBuf_t{
	int busy;
	int broadcastid;
	struct clientReqMsg msg;
};

struct hwa_info *interfaces_G;
int sunfd_ODR_G;
int pfsockfd_ODR_G;
int staleness_G = 300;
int broadcastid_G = 0;
in_addr_t myIP_G;
char hostname_G[100];
struct routing_table_entry  routing_table_G[MAX_ROUTES];
struct ephemeral_port ephemeral_ports_G[MAX_PORTS]; 

int assignPort(char *client_sun_path);
int hasValidRoute(char *dest, in_addr_t ndest);
void doRREQ(int sockfd, int broadcasd_id, struct clientReqMsg *cli_msg,
       int ifindex_inc);
void doRREP(void *eth_msg, int hopcount);
void relayRREQ(void *eth_msg, int ifindex_inc, int setrepsent);
void relayRREP(void *eth_msg_buffer);
void dump_routingTable();
void printHaddr(char *haddr);
void relay_appl(void *eth_frame);
void printRREX(void *eth_msg_buffer);

int main(int argc, const char *argv[]){

	struct sockaddr_un sun_odr;
	struct sockaddr_ll spf_odr;
	struct ethhdr *eh;
	struct clientReqMsg odrClientMsg;
	struct clientMsgBuf_t clientMsgBuf[MAX_CLIENT_MSG_BUF];
	struct ODR_RREX_t *applMsg;
	struct ODR_RREX_t recvdRREXMsg;
	struct hwa_info *iptr = NULL;
	struct sockaddr_in *sin;
	struct hostent *hent;
  struct in_addr	aton_in_addr;
	char ipa[20], eth_msg_buffer[sizeof(struct ethhdr) + sizeof(struct ODR_RREX_t)];
	struct in_addr myIP;
	struct recvdRREQLog_t recvdRREQLog[MAX_RECVDRREQS_LOG];
	fd_set rset;
	char *ptr;
	int i, maxfdp1, len, rid, assgnport, appInBuf, reverseRouteUpdated=0, ignoreRREQ=0, freeslot, recvdType, recvdBroadcastid, recvdSrc,
		 	recvdDest, recvdHopcount, recvdSport, recvdDport, recvdSize, recvdrepsent, recvdForceRedisc;
	
	if(argc == 2){
		staleness_G = atoi(argv[1]);
	}
	interfaces_G = Get_hw_addrs();

	
	printf("INTERFACES:\n");
	iptr = interfaces_G;
	while (iptr != NULL){
		i = ETH_ALEN;
		printf("%s: index=%d ",iptr->if_name, iptr->if_index);
		ptr = iptr->if_haddr;
		do {
		     printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " " : ":");
		 } while (--i > 0);
		printf("\n");
		iptr = iptr->hwa_next;

	}
  gethostname(hostname_G, sizeof(hostname_G));
	iptr = interfaces_G;
	while (iptr != NULL){
		if (strcmp(iptr->if_name, "eth0") == 0){
			printf("Found eth0.\n");
		  sin = (struct sockaddr_in *) iptr->ip_addr;
			memcpy(&myIP_G, &sin->sin_addr.s_addr, sizeof( sin->sin_addr.s_addr));
			printf("I am %s (%s)\n", hostname_G, Sock_ntop_host(iptr->ip_addr, sizeof(*iptr->ip_addr)));
			break;
		}
		iptr = iptr->hwa_next;
	}
	pfsockfd_ODR_G = Socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ODR));
	sunfd_ODR_G = Socket(AF_UNIX, SOCK_DGRAM, 0);
	sun_odr.sun_family = AF_UNIX;
	strncpy (sun_odr.sun_path, SUN_PATH_ODR, sizeof (sun_odr.sun_path));
	sun_odr.sun_path[sizeof (sun_odr.sun_path) - 1] = '\0';
	unlink(sun_odr.sun_path);
	Bind(sunfd_ODR_G, (SA *)&sun_odr, SUN_LEN(&sun_odr));
	strcpy(ephemeral_ports_G[SERVER_PORT].path, SERVER_SUN_PATH); 
	ephemeral_ports_G[SERVER_PORT].busy = 1;

	printf("Staleness is %d, sun_path is %s.\n", staleness_G, sun_odr.sun_path );
	FD_ZERO(&rset);
	for( ; ; ){
		FD_SET(sunfd_ODR_G, &rset);
		FD_SET(pfsockfd_ODR_G, &rset);
		maxfdp1 = max(sunfd_ODR_G, pfsockfd_ODR_G) + 1;
		Select(maxfdp1, &rset, NULL, NULL, NULL);

		if (FD_ISSET(pfsockfd_ODR_G, &rset)){
			printf("Received message on PF_PACKET socket.\n");
			len = sizeof(spf_odr);
			Recvfrom(pfsockfd_ODR_G, eth_msg_buffer, ODR_ETH_MSG_SIZE, 0, (SA*)&spf_odr, &len);
			eh = (struct ethhdr*) eth_msg_buffer;
			memcpy(&recvdRREXMsg, eth_msg_buffer + sizeof(struct ethhdr), sizeof(struct ODR_RREX_t));
			recvdType = ntohl(recvdRREXMsg.type);
			recvdBroadcastid = ntohl(recvdRREXMsg.broadcastid);
			recvdSrc = recvdRREXMsg.src;
			recvdDest = recvdRREXMsg.dest;
			recvdHopcount = ntohl(recvdRREXMsg.hopcount);
			recvdrepsent = ntohl(recvdRREXMsg.repsent);
			recvdSport = ntohl(recvdRREXMsg.sport);
			recvdDport = ntohl(recvdRREXMsg.dport);
			recvdSize = ntohl(recvdRREXMsg.size);
			recvdForceRedisc = ntohl(recvdRREXMsg.forceRedisc);

			printf("ODR_RREX:\
										\n\t Type = %d\
										\n\t Broadcastid = %d\
										\n\t Hopcount = %d\
										\n\t Sport = %d\
										\n\t Dport = %d\
										\n\t Data = %s\
										\n\t Size = %d\
										\n\t RepSent = %d\
										\n\t ForceRedisc = %d\
										 ", recvdType, recvdBroadcastid, recvdHopcount, recvdSport, recvdDport, recvdRREXMsg.data, 
										 	 recvdSize, recvdrepsent, recvdForceRedisc);
			aton_in_addr.s_addr = recvdSrc;							 
			// copy return value of inet_ntoa because return value is static and it can get overwritten
			// by a new call to inet_ntoa
			// strcpy(ipa, inet_ntoa(aton_in_addr), strlen(inet_ntoa(aton_in_addr)));
			hent = gethostbyaddr(&aton_in_addr, sizeof(aton_in_addr), AF_INET);
			printf("\n\t Src = %s(%s)", inet_ntoa(aton_in_addr), hent->h_name);
			aton_in_addr.s_addr = recvdDest;							 
			hent = gethostbyaddr(&aton_in_addr, sizeof(aton_in_addr), AF_INET);
			printf("\n\t Dest = %s(%s)\n", inet_ntoa(aton_in_addr), hent->h_name);

			reverseRouteUpdated = 0;
			ignoreRREQ = 0;
	
			if ((rid = hasValidRoute(NULL, recvdSrc))){
				// Check which route is better, the one we already have or the reverse path 
				if (routing_table_G[rid].hopcount > recvdHopcount){
					printf("Reverse route is better than what we have.\n");
					routing_table_G[rid].hopcount = recvdHopcount;
					routing_table_G[rid].ifindex = spf_odr.sll_ifindex;
					memcpy(routing_table_G[rid].nexthop, eh->h_source, ETH_ALEN);
					gettimeofday(&routing_table_G[rid].time, NULL);
					reverseRouteUpdated = 1;
				}
				else if(routing_table_G[rid].hopcount == recvdHopcount
								&& memcmp(routing_table_G[rid].nexthop, &eh->h_source, ETH_ALEN) != 0){
				
					printf("Reverse route hopcount = the route we have but reverse route is now different.\n");
					memcpy(routing_table_G[rid].nexthop, &eh->h_source, ETH_ALEN);
					gettimeofday(&routing_table_G[rid].time, NULL);
				 	reverseRouteUpdated = 1;
				}
				else{
					printf("The route we have is better than reverse path.\n");
				}
			}
			else {
				printf("We don't have reverse route. Add this.\n");
				fflush(stdout);
				for(i=1;i<MAX_ROUTES;i++){
					if(routing_table_G[i].valid == 0){
						routing_table_G[i].valid = 1;
						routing_table_G[i].dest = recvdSrc;
						routing_table_G[i].hopcount = recvdHopcount + 1;
						routing_table_G[i].ifindex = spf_odr.sll_ifindex;
						memcpy(routing_table_G[i].nexthop, eh->h_source, ETH_ALEN);
						gettimeofday(&routing_table_G[i].time, NULL);
						reverseRouteUpdated = 1;
						printf("Reverse Route Added.\n");
						dump_routingTable();
						fflush(stdout);
						break;
					}
				}

			}

			switch(recvdType){
				case RREQ:
								printf("Msg is of type RREQ.\n");
								// Check if it is a duplicate RREQ we have already processed, if not record it in log
								for (i=0;i<MAX_RECVDRREQS_LOG;i++){
									if(recvdRREQLog[i].valid == 1 
										&& recvdRREQLog[i].src == recvdSrc 
										&& recvdRREQLog[i].broadcastid == recvdBroadcastid
										&& recvdRREQLog[i].hopcount <= recvdHopcount){
										
										printf("Duplicate RREQ and hopcount is worse. Ignore\n");
										ignoreRREQ = 1;
										break;
									}
								}
								if(ignoreRREQ)
									break;
								else {
									for (i=0;i<MAX_RECVDRREQS_LOG;i++){
										if(recvdRREQLog[i].valid == 0){
											freeslot = i;
											break;
										}
									}
									recvdRREQLog[freeslot].valid = 1;
									recvdRREQLog[freeslot].src = recvdSrc;
									recvdRREQLog[freeslot].broadcastid = recvdBroadcastid;
									recvdRREQLog[freeslot].hopcount = recvdHopcount;
								}

								if (recvdDest == myIP_G){
									printf("RREQ is for me. do RREP.\n");
									doRREP(eth_msg_buffer, -1);
								}
								else{
								// If we have route to dest then do RREP if reply sent is not set
								// and relay RREQ after setting reply sent 
								  if (!recvdrepsent){
								  	if ((rid = hasValidRoute(NULL, recvdDest))){

												if(recvdForceRedisc == 1){
													printf("Force rediscovery is set but I have route so flush it.");
													routing_table_G[rid].valid = 0;
								  				relayRREQ(eth_msg_buffer, spf_odr.sll_ifindex, 0);
												}
												else{
								  			printf("Reply is not already sent to this RREQ\
								  										 	and we have route. Do RREP and relay RREQ and set reply sent.\n");
								  				doRREP(eth_msg_buffer, routing_table_G[rid].hopcount);
								  		  	relayRREQ(eth_msg_buffer, spf_odr.sll_ifindex, 1);
												}
								  		}
								  	else {
								  		printf("We dont have route to dest. So relay RREQ.\n");
								  		relayRREQ(eth_msg_buffer, spf_odr.sll_ifindex, 0);
								  	}
									}
								  else{
								  	printf("reply sent is already set\n");
									}
								}
								

								break;
					
				case RREP:
								appInBuf = 0;
								printf("Msg is of type RREP.\n");
								if(recvdDest == myIP_G){
									printf("RREP is for me, now I have route so send pending app payload.\n");
									// find pending app payload using broadcastid
									for(i=0;i<MAX_CLIENT_MSG_BUF;i++){
										if(clientMsgBuf[i].broadcastid == recvdBroadcastid){
											appInBuf = 1;
											odrClientMsg = clientMsgBuf[i].msg;
											clientMsgBuf[i].busy = 0;
											break;
										}
									}
									if (!appInBuf) {
										printf("Cannot find pending app payload.\n");
									}
									else {
										goto sendappl;
									}
								}
								else {
									printf("RREP is not for me, relay RREP.\n");
										relayRREP(eth_msg_buffer);
								}

								break;
				
				case APPL:
								relay_appl(eth_msg_buffer);

								break;
			
				default:
								printf("Unknown ODR message type.\n");
			
			}

}

		if (FD_ISSET(sunfd_ODR_G, &rset)){
			printf("Received message on UNIX DOMAIN socket.\n");
			bzero(&sun_odr, sizeof(sun_odr));
			len = sizeof(sun_odr);
			Recvfrom(sunfd_ODR_G, &odrClientMsg, sizeof(struct clientReqMsg), 0, (SA*)&sun_odr, &len);
			printf("odrClientMsg:\
											\n\t Dest = %s\
											\n\t Port = %d\
											\n\t Data = %s\
											\n\t Flag = %d\
											\n\t Len = %d\
											\n\t sun_path = %s\n", odrClientMsg.dest, odrClientMsg.port, odrClientMsg.data, 
					 																	 odrClientMsg.flag, len, sun_odr.sun_path);

				assgnport = assignPort(sun_odr.sun_path);
sendappl:
			if ((rid = hasValidRoute(odrClientMsg.dest, 0)) && !odrClientMsg.flag){

				printf("Has route and force flag is not set. Send application without RREQ.\n");
				spf_odr.sll_family = PF_PACKET;
				spf_odr.sll_protocol = htons(ETH_P_ODR);
				spf_odr.sll_ifindex = routing_table_G[rid].ifindex;
				spf_odr.sll_pkttype  = PACKET_OTHERHOST;
				spf_odr.sll_halen = ETH_ALEN;
				printf("nexthop=");
				printHaddr(routing_table_G[rid].nexthop);
				printf("\n");
				memcpy(spf_odr.sll_addr, routing_table_G[rid].nexthop, ETH_ALEN);
				eh = (struct ethhdr *) eth_msg_buffer;
				memcpy(&eh->h_dest, routing_table_G[rid].nexthop, ETH_ALEN);
				iptr = interfaces_G;
				while (iptr != NULL){
					if (iptr->if_index == routing_table_G[rid].ifindex)
							break;
					iptr = iptr->hwa_next;
				}
				memcpy(&eh->h_source, iptr->if_haddr, ETH_ALEN);
				printf("saddr=");
				printHaddr(eh->h_source);
				printf("\n");
				eh->h_proto = htons(ETH_P_ODR);
				applMsg = (struct ODR_RREX_t *)(eth_msg_buffer + sizeof(struct ethhdr));
				applMsg->type = htonl(APPL);
				applMsg->hopcount = 0;
				applMsg->repsent = 0;
				applMsg->broadcastid = htonl(broadcastid_G++);
				applMsg->src = myIP_G;
				inet_aton(odrClientMsg.dest, &aton_in_addr);
				applMsg->dest = aton_in_addr.s_addr;
				applMsg->sport = htonl(assgnport);
				applMsg->dport = htonl(odrClientMsg.port);
				strcpy(applMsg->data, odrClientMsg.data);
				applMsg->size = htonl(strlen(odrClientMsg.data) + 1);
				Sendto(pfsockfd_ODR_G, eth_msg_buffer, ODR_ETH_MSG_SIZE, 0, (SA*)&spf_odr, sizeof(spf_odr));
			}
			else {
				if (odrClientMsg.flag){
					printf("Force route discovery is set. do RREQ\n");
				}
				else
					printf("No route for %s doRREQ.\n", odrClientMsg.dest);
				fflush(stdout);
				for(i=0;i<MAX_CLIENT_MSG_BUF;i++){
					if(clientMsgBuf[i].busy == 0){
						clientMsgBuf[i].busy = 1;
						clientMsgBuf[i].broadcastid = broadcastid_G;
						memcpy(&clientMsgBuf[i].msg, &odrClientMsg, sizeof(odrClientMsg));
						break;
					}
				}
				doRREQ(pfsockfd_ODR_G, broadcastid_G++, &odrClientMsg, -1);
			}
		}

	}
}


int assignPort(char *client_sun_path){
	int i, freePort;
	struct timeval tv;
	printf("in assignPort: client_sun_path=%s.\n", client_sun_path);
	for (i = 1;i<MAX_PORTS;i++){
		if (ephemeral_ports_G[i].busy == 1 && strcmp(client_sun_path, ephemeral_ports_G[i].path) == 0){
			printf("Client already has a port %d assigned\n", i);
			gettimeofday(&ephemeral_ports_G[i].time, NULL);
			return i;
		}
		else if (ephemeral_ports_G[i].busy == 1){
			// This means port is in use. Make it available to reuse if it is too old
			gettimeofday(&tv, NULL);
			if(tv.tv_sec - ephemeral_ports_G[i].time.tv_sec  > OLD_PORT_TIMER);
				printf("Making port %d re-usable.\n", i);
				ephemeral_ports_G[i].busy = 0;
				freePort = i;
		}
		else{
		  // This port is free, store it in case at the end of iteration we found this client needs a port  
			  freePort = i;
		}
	}
	
	for (i = 1;i<MAX_PORTS;i++){
		if (ephemeral_ports_G[i].busy == 0){
			freePort = i;
			break;
		}
	}
	ephemeral_ports_G[freePort].busy = 1;
	strcpy(ephemeral_ports_G[freePort].path, client_sun_path);
	gettimeofday(&ephemeral_ports_G[freePort].time, NULL);
  printf("Assigned port %d to client %s\n", freePort, ephemeral_ports_G[freePort].path);
	return freePort;
}


// Can look for dest in routing table. Can do lookup with dest as char * or in_addr_t
int hasValidRoute(char *dest, in_addr_t ndest){
	struct in_addr sin;
	struct timeval tv;
	struct routing_table_entry rte;
	int i;

	dump_routingTable();
	if (dest!=NULL)
		inet_aton(dest, &sin);
	else
		sin.s_addr = ndest;

	printf("hasValidRoute: looking for %s (%u).\n",inet_ntoa(sin), sin.s_addr);
	gettimeofday(&tv, NULL);
	for(i=1;i<MAX_ROUTES;i++){ // start from 1 because return value 0 means no route
		rte = routing_table_G[i];
		if (rte.valid == 1 && rte.dest == sin.s_addr){
			printf("Found route to dest %s\n", inet_ntoa(sin));
			if ((tv.tv_sec - rte.time.tv_sec) < staleness_G)
				return i;
			else
				printf("Route to dest %s expired.\n", inet_ntoa(sin));
				routing_table_G[i].valid = 0;
				return 0;
		}
	}
	printf("No route to dest %s.\n", inet_ntoa(sin));
	return 0;
}

/* broadcast RREQs on all interfaces except lo, eth0 and incoming interface.
 * ifindex_in is -1 for no incoming interface
 */
void doRREQ(int sockfd, int broadcast_id, struct clientReqMsg *cli_msg,
      int ifindex_inc)
{
  struct sockaddr_ll rreq_sock;
  struct ethhdr *eh;
  char eth_msg_buffer[sizeof(struct ethhdr) + sizeof(struct ODR_RREX_t)];
  struct ODR_RREX_t *rreq_msg;
  struct in_addr atonsin;
  unsigned char dest_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  struct hwa_info *iptr = NULL;


	printf("doRREQ: sending\n");
  /* create a link layer (ll) packet */
  rreq_sock.sll_family = AF_PACKET;
  rreq_sock.sll_protocol = htons(ETH_P_ODR);
  rreq_sock.sll_halen = ETH_ALEN;

  /* fill in frame header */
  eh = (struct ethhdr *) eth_msg_buffer;
  memcpy(&eh->h_dest, (void *)dest_mac, ETH_ALEN);
  eh->h_proto = htons(ETH_P_ODR);

  /* fill in payload message */
  rreq_msg = (struct ODR_RREX_t *)(eth_msg_buffer + sizeof(struct ethhdr));
  rreq_msg->type = htonl(RREQ);
  rreq_msg->hopcount = htonl(-1);
  rreq_msg->repsent = 0;
  rreq_msg->broadcastid = htonl(broadcast_id);
  rreq_msg->forceRedisc = cli_msg->flag == 1 ? htonl(1)  : 0;
  rreq_msg->src = myIP_G;
  inet_aton(cli_msg->dest, &atonsin);
  rreq_msg->dest = atonsin.s_addr;
  rreq_msg->dport = htonl(cli_msg->port);
  strcpy(rreq_msg->data, cli_msg->data);
  rreq_msg->size = htonl(strlen(cli_msg->data) + 1);

  /* broadcast on all interfaces except lo, eth0, incoming rreq interface */
  iptr = interfaces_G;
  while (iptr != NULL){
    if ( !strcmp(iptr->if_name, "lo") || !strcmp(iptr->if_name, "eth0")){
    	iptr = iptr->hwa_next;
      continue;
		}
    if (iptr->if_index == ifindex_inc){
    	iptr = iptr->hwa_next;
      continue;
		}

    /* fill in empty fields of frame */
    memcpy(&eh->h_source, iptr->if_haddr, ETH_ALEN);
    memcpy(rreq_sock.sll_addr, dest_mac, ETH_ALEN);
    rreq_sock.sll_ifindex = iptr->if_index;
		printRREX(eth_msg_buffer);
    Sendto(sockfd, eth_msg_buffer, ODR_ETH_MSG_SIZE, 0,
      (SA*)&rreq_sock, sizeof(rreq_sock));
    
		iptr = iptr->hwa_next;

  }
}

void relayRREQ(void *eth_msg, int ifindex_inc, int setrepsent){
  struct sockaddr_ll rreq_sock;
  struct ethhdr *eh;
  struct ODR_RREX_t *rreq_msg;
  struct in_addr atonsin;
  unsigned char dest_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  struct hwa_info *iptr = NULL;

	printf("relayRREQ: sending\n");
  /* create a link layer (ll) packet */
  rreq_sock.sll_family = AF_PACKET;
  rreq_sock.sll_protocol = htons(ETH_P_ODR);
  rreq_sock.sll_halen = ETH_ALEN;
	
	eh = (struct ethhdr *) eth_msg;
	memcpy(&eh->h_dest, (void *)dest_mac, ETH_ALEN);
  
	rreq_msg = (struct ODR_RREX_t *)(eth_msg + sizeof(struct ethhdr));
	rreq_msg->hopcount = htonl( ntohl(rreq_msg->hopcount) + 1);
	
	if(setrepsent)
		rreq_msg->repsent = htonl(1);

  
	/* broadcast on all interfaces except lo, eth0, incoming rreq interface */
  iptr = interfaces_G;
  while (iptr != NULL){
    if ( !strcmp(iptr->if_name, "lo") || !strcmp(iptr->if_name, "eth0")){
    	iptr = iptr->hwa_next;
      continue;
		}
    if (iptr->if_index == ifindex_inc){
    	iptr = iptr->hwa_next;
      continue;
		}

    /* fill in empty fields of frame */
    memcpy(&eh->h_source, iptr->if_haddr, ETH_ALEN);
    memcpy(rreq_sock.sll_addr, dest_mac, ETH_ALEN);
    rreq_sock.sll_ifindex = iptr->if_index;
	printRREX(eth_msg);
    Sendto(pfsockfd_ODR_G, eth_msg, ODR_ETH_MSG_SIZE, 0,
      (SA*)&rreq_sock, sizeof(rreq_sock));

    iptr = iptr->hwa_next;
  }
}

void relayRREP(void *eth_msg){
  struct sockaddr_ll rreq_sock;
  struct ethhdr *eh;
  struct ODR_RREX_t *rreq_msg;
  struct in_addr atonsin;
  struct hwa_info *iptr = NULL;
	in_addr_t repDest;
	int ridRepDest;


	printf("relay_appl: sending\n");


  /* create a link layer (ll) packet */
  rreq_sock.sll_family = AF_PACKET;
  rreq_sock.sll_protocol = htons(ETH_P_ODR);
  rreq_sock.sll_halen = ETH_ALEN;

	eh = (struct ethhdr *) eth_msg;
	rreq_msg = (struct ODR_RREX_t *)(eth_msg + sizeof(struct ethhdr));
	rreq_msg->type = htonl(RREP);
	rreq_msg->hopcount = htonl(ntohl(rreq_msg->hopcount) + 1);

	repDest = rreq_msg->dest;
	

	
	if ( !(ridRepDest = hasValidRoute(NULL, repDest) )){
		printf("Cannot relay RREP, no reverse route to destination.\n");
		return;		
	}
  
  iptr = interfaces_G;
	while (iptr != NULL){
    if ( iptr->if_index == routing_table_G[ridRepDest].ifindex)
			break;
		iptr = iptr->hwa_next;
	}
	memcpy(&eh->h_source, &iptr->if_haddr, ETH_ALEN);
	memcpy(&eh->h_dest, &routing_table_G[ridRepDest].nexthop, ETH_ALEN);
//  printf("Relaying RREP to mac address ");
//	printHaddr(eh->h_dest);
//	printf("\n");
//  printf("Relaying RREP from mac address ");
//	printHaddr(eh->h_source);
//	printf("\n");
  memcpy(rreq_sock.sll_addr, &routing_table_G[ridRepDest].nexthop, ETH_ALEN);
  rreq_sock.sll_ifindex = iptr->if_index;
	printf(" ehproto=%x, RREP data=%s\n", ntohs(eh->h_proto), rreq_msg->data);
	printRREX(eth_msg);
	Sendto(pfsockfd_ODR_G, eth_msg, ODR_ETH_MSG_SIZE, 0,
      (SA*)&rreq_sock, sizeof(rreq_sock));
}
// hopcount must be -1 when destination of RREQ is me and I am replying for myself. 
void doRREP(void *eth_msg, int hopcount){
  struct sockaddr_ll rreq_sock;
  struct ethhdr *eh;
  struct ODR_RREX_t *rreq_msg;
  struct in_addr atonsin;
  unsigned char dest_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  struct hwa_info *iptr = NULL;
	in_addr_t repDest;
	in_addr_t tmp;
	int ridRepDest;

	printf("doRREP: sending\n");

  /* create a link layer (ll) packet */
  rreq_sock.sll_family = AF_PACKET;
  rreq_sock.sll_protocol = htons(ETH_P_ODR);
  rreq_sock.sll_halen = ETH_ALEN;

	eh = (struct ethhdr *) eth_msg;
	rreq_msg = (struct ODR_RREX_t *)(eth_msg + sizeof(struct ethhdr));
	rreq_msg->type = htonl(RREP);
	rreq_msg->hopcount = htonl(hopcount);

	repDest = rreq_msg->src;
	// swap rreq_msg->src with rreq_msg->dest
	tmp = rreq_msg->src;
	rreq_msg->src = rreq_msg->dest;
	rreq_msg->dest = tmp;
	

	
	if ( !(ridRepDest = hasValidRoute(NULL, repDest) )){
		printf("Cannot do RREP, no reverse route to destination.\n");
		return;		
	}
  
  iptr = interfaces_G;
	while (iptr != NULL){
    if ( iptr->if_index == routing_table_G[ridRepDest].ifindex)
			break;
		iptr = iptr->hwa_next;
	}
	memcpy(&eh->h_source, &iptr->if_haddr, ETH_ALEN);
	memcpy(&eh->h_dest, &routing_table_G[ridRepDest].nexthop, ETH_ALEN);
//  printf("Sending RREP to mac address ");
//	printHaddr(eh->h_dest);
//	printf("\n");
//  printf("Sending RREP from mac address ");
//	printHaddr(eh->h_source);
//	printf("\n");
  memcpy(rreq_sock.sll_addr, &routing_table_G[ridRepDest].nexthop, ETH_ALEN);
  rreq_sock.sll_ifindex = iptr->if_index;
	printf(" ehproto=%x, RREP data=%s\n", ntohs(eh->h_proto), rreq_msg->data);
	printRREX(eth_msg);
	Sendto(pfsockfd_ODR_G, eth_msg, ODR_ETH_MSG_SIZE, 0,
      (SA*)&rreq_sock, sizeof(rreq_sock));
}

void dump_routingTable(){
	int i, j;
	struct in_addr sin;
	struct hostent *hent;
	char *ptr;
	printf("DUMP ROUTES for %s:\n", hostname_G);
	printf("---------------------\n");
	for(i=1;i<MAX_ROUTES;i++){
		if(routing_table_G[i].valid == 1){
			sin.s_addr = routing_table_G[i].dest;
			hent = gethostbyaddr(&sin, sizeof(sin), AF_INET);
			printf("Route %d: Dest_n=%s(%s), Ifindex=%d,Hopcount=%d, Time=%ld", 
											i, inet_ntoa(sin),hent->h_name, 
											routing_table_G[i].ifindex, routing_table_G[i].hopcount, (long int)routing_table_G[i].time.tv_sec);
			printf(" Nexthop=");
			j=ETH_ALEN;
			ptr = routing_table_G[i].nexthop;
			do {
		     printf("%.2x%s", *ptr++ & 0xff, (j == 1) ? " " : ":");
		 	} while (--j > 0);
			printf("\n");
		}
	}
	printf("---------------------\n");
}

void printHaddr(char *haddr){
	int j;
			j=ETH_ALEN;
			do {
		     printf("%.2x%s", *haddr++ & 0xff, (j == 1) ? " " : ":");
		 	} while (--j > 0);
}
void relay_appl(void *eth_frame)
{
  struct ethhdr *eh = (struct ethhdr *)eth_frame;
  struct sockaddr_ll appl_sock;
  struct sockaddr_un serv;
	struct in_addr sin;
  struct ODR_RREX_t *odr_pkt;
  struct clientRespMsg msg;
  struct hwa_info *iptr = NULL;
  int id, dport;

	printf("relay_appl:\n");

  odr_pkt = (struct ODR_RREX_t *)((char *)eth_frame + sizeof(struct ethhdr));

  /* check if i am the destination.
   * If yes, pass the application message to the uppper layer.
   * If not, forward the application message to the next hop
   */
  if (myIP_G == odr_pkt->dest) {
    /* inititalizae server socket address */
    printf("Application payload is for me\n");
    bzero(&serv, sizeof(serv));
    serv.sun_family = AF_LOCAL;
		dport = ntohl(odr_pkt->dport);
    if (!ephemeral_ports_G[dport].busy)
      printf("port %d is not busy\n", dport);
    strcpy(serv.sun_path, ephemeral_ports_G[dport].path);
    gettimeofday(&ephemeral_ports_G[dport].time, NULL);

    msg.port = ntohl(odr_pkt->sport);
    strcpy(msg.data, odr_pkt->data);
		sin.s_addr = odr_pkt->src;
    strcpy(msg.src, inet_ntoa(sin));
    printf("Odr sending application payload back to %s\n", serv.sun_path);
    if( sendto(sunfd_ODR_G, &msg, sizeof(struct clientRespMsg), 0, (SA *)&serv, sizeof(serv)) 
										!= sizeof(struct clientRespMsg)  ){
			printf("Cannot send message back to odr user. Is user still running?\n");
		}
  } else {
    printf("Relaying application payload\n");
    /* get the next hop */
    id = hasValidRoute(NULL, odr_pkt->dest);
    if (!id) {
      printf("No route found\n");
      return;
    }
    iptr = interfaces_G;
    while (iptr != NULL){
      if ( iptr->if_index == routing_table_G[id].ifindex) {
        memcpy(eh->h_source, iptr->if_haddr, ETH_ALEN);
        break;
      }
      iptr = iptr->hwa_next;
    }
		printf("id=%d ifindex=%d\n", id, routing_table_G[id].ifindex);
		printf("iptr_haddr");
		printHaddr(iptr->if_haddr);
		printf("\n");
		printf("sourceaddr");
		printHaddr(eh->h_source);
		printf("\n");
    memcpy(eh->h_dest, routing_table_G[id].nexthop, ETH_ALEN);
		printf("routing nexthop");
		printHaddr(routing_table_G[id].nexthop);
		printf("\n");
		printf("destaddr");
		printHaddr(eh->h_dest);
		printf("\n");

    /* create a link layer (ll) packet */
    appl_sock.sll_family = AF_PACKET;
    appl_sock.sll_protocol = htons(ETH_P_ODR);
    appl_sock.sll_halen = ETH_ALEN;
    memcpy(appl_sock.sll_addr, routing_table_G[id].nexthop, ETH_ALEN);
    appl_sock.sll_ifindex = routing_table_G[id].ifindex;
    odr_pkt->hopcount = htonl(ntohl(odr_pkt->hopcount) + 1);
	printRREX(eth_frame);
    Sendto(pfsockfd_ODR_G, eth_frame, sizeof(struct ethhdr) + sizeof(struct ODR_RREX_t), 0,
         (SA *)&appl_sock, sizeof(appl_sock));
  }
}

void printRREX(void *eth_msg_buffer)
{
			struct ODR_RREX_t recvdRREXMsg;
			struct ethhdr *eh;
			int recvdType, recvdBroadcastid, recvdSrc, recvdDest, recvdHopcount, recvdrepsent, recvdSport, recvdDport, recvdSize,
				 recvdForceRedisc;	
			struct in_addr aton_in_addr;
			struct hostent *hent;

			eh = (struct ethhdr*) eth_msg_buffer;
			memcpy(&recvdRREXMsg, (char *)eth_msg_buffer + sizeof(struct ethhdr), sizeof(struct ODR_RREX_t));
			recvdType = ntohl(recvdRREXMsg.type);
			recvdBroadcastid = ntohl(recvdRREXMsg.broadcastid);
			recvdSrc = recvdRREXMsg.src;
			recvdDest = recvdRREXMsg.dest;
			recvdHopcount = ntohl(recvdRREXMsg.hopcount);
			recvdrepsent = ntohl(recvdRREXMsg.repsent);
			recvdSport = ntohl(recvdRREXMsg.sport);
			recvdDport = ntohl(recvdRREXMsg.dport);
			recvdSize = ntohl(recvdRREXMsg.size);
			recvdForceRedisc = ntohl(recvdRREXMsg.forceRedisc);

			printf("ODR_RREX:\
										\n\t Type = %d\
										\n\t Broadcastid = %d\
										\n\t Hopcount = %d\
										\n\t Sport = %d\
										\n\t Dport = %d\
										\n\t Data = %s\
										\n\t Size = %d\
										\n\t RepSent = %d\
										\n\t ForceRedisc = %d\
										 ", recvdType, recvdBroadcastid, recvdHopcount, recvdSport, recvdDport, recvdRREXMsg.data, 
										 	 recvdSize, recvdrepsent, recvdForceRedisc);
			aton_in_addr.s_addr = recvdSrc;							 
			// copy return value of inet_ntoa because return value is static and it can get overwritten
			// by a new call to inet_ntoa
			// strcpy(ipa, inet_ntoa(aton_in_addr), strlen(inet_ntoa(aton_in_addr)));
			hent = gethostbyaddr(&aton_in_addr, sizeof(aton_in_addr), AF_INET);
			printf("\n\t Src = %s(%s)", inet_ntoa(aton_in_addr), hent->h_name);
			aton_in_addr.s_addr = recvdDest;							 
			hent = gethostbyaddr(&aton_in_addr, sizeof(aton_in_addr), AF_INET);
			printf("\n\t Dest = %s(%s)\n", inet_ntoa(aton_in_addr), hent->h_name);
			fflush(stdout);
}
