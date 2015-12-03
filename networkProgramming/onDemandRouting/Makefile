
CC = gcc

LIBS =  /home/courses/cse533/Stevens/unpv13e/libunp.a

FLAGS = -ggdb -O2
CFLAGS = -I/home/courses/cse533/Stevens/unpv13e/lib

all: odr server client

odr: odr.c get_hw_addrs.o
	${CC} ${FLAGS} ${CFLAGS} -o odr_kpugla odr.c get_hw_addrs.o ${LIBS}

get_hw_addrs.o: get_hw_addrs.c
	${CC} ${FLAGS} -c get_hw_addrs.c

client: client.c
	  ${CC} ${FLAGS} ${CFLAGS} -o client_kpugla client.c  ${LIBS}

server: server.c
	  ${CC} ${FLAGS} ${CFLAGS} -o server_kpugla server.c  ${LIBS}

clean:
	rm -rf odr_kpugla get_hw_addrs.o client_kpugla server_kpugla

