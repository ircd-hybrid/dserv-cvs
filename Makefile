BINARY = dserv
SRCS = commands.c dserv.c irc_commands.c irc_io.c irc_users.c net.c \
       svc_io.c irc_channel.c database.c config.c oomon_io.c blockheap.c \
       clones.c
LIBS=-lefence
OBJS = ${SRCS:.c=.o} ${LIBS}
MKDEP = gcc -MM
CC = gcc
CFLAGS=-Wall -ggdb
LDFLAGS=
INCLUDES=-I inc
all: ${BINARY}
${BINARY}: ${OBJS}

.c.o:
	${CC} ${CFLAGS} ${INCLUDES} -c $<

.PHONY : depend
depend:
	${MKDEP} ${INCLUDES} ${SRCS} > .depend

${BINARY}: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} -o ${BINARY}
include .depend
