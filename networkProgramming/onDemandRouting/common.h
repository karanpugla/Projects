#ifndef __a3_common_h
#define __a3_common_h


char * const SUN_PATH_ODR = "/tmp/110452661_odr";
char *const SERVER_SUN_PATH = "/tmp/110452661_server";
struct clientReqMsg{
	char dest[16];
	int port;
	int flag;
	char data[64];
};
struct clientRespMsg{
	char data[64];
	char src[16];
	int port;
};

#endif

