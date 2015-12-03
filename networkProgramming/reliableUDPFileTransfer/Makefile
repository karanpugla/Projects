# This is a Makefile which compiles source files named:
# - client.c
# - server.c
# and creating executables: "server", "client" 
#
# It uses various standard libraries, and the copy of Stevens'
# library "libunp.a" in ~cse533/Stevens/unpv13e_solaris2.10 .
#

CC = gcc

LIBS = -lresolv -lsocket -lnsl -lpthread -lm\
	/home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a\
	
FLAGS = -g -O2 

CFLAGS = ${FLAGS} -ggdb  -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

all: client server 



server: server.o get_ifi_info_plus.o myhdr.o rttplus.o
	${CC} ${FLAGS} -o server server.o get_ifi_info_plus.o myhdr.o rttplus.o ${LIBS}
server.o: server.c
	${CC} ${CFLAGS} -c server.c
get_ifi_info_plus.o: get_ifi_info_plus.c 
	${CC} ${CFLAGS} -c get_ifi_info_plus.c
myhdr.o: myhdr.c 
	${CC} ${CFLAGS} -c myhdr.c
rttplus.o: rttplus.c 
	${CC} ${CFLAGS} -c rttplus.c

client: client.o myhdr.o get_ifi_info_plus.o
	${CC} ${FLAGS} -o client client.o myhdr.o get_ifi_info_plus.o ${LIBS}
client.o: client.c
	${CC} ${CFLAGS} -c client.c

test: test.o  myhdr.o
	${CC} ${FLAGS} -o test test.o myhdr.o ${LIBS}
test.o: test.c
	${CC} ${CFLAGS} -c test.c

clean:
	rm -f  server server.o client client.o test test.o rttplus.o myhdr.o


