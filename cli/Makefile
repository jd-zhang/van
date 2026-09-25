CC=	gcc
CFLAGS=	-c -g $(OUTCFLAGS)
LDFLAGS= -g -Wall $(OUTCFLAGS)
CPPFLAGS= -I../include -I../include_auto -D_REENTRANT -D_GNU_SOURCE

# Every vanilla object lives in the root of the tree, link them straight
# into the shell so that it does not need the shared libvan.a at run time.
VAN_OBJS= $(sort $(wildcard ../*.o))
CLIBS=	-lpthread -lm

all: vansql

vansql: vansql.o $(VAN_OBJS)
	$(CC) -o $@ $(LDFLAGS) vansql.o $(VAN_OBJS) $(CLIBS)

vansql.o: vansql.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ vansql.c

../libvan.a:
	cd .. && $(MAKE) libvan.a

clean:
	rm -f vansql.o vansql.exe vansql
