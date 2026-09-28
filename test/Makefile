# Created by create_makefile.sh, please do not edit it! 
CC=	gcc
CXX=	g++
CPPFLAGS=	-D_GNU_SOURCE -D_REENTRANT -I.. -I../include -I../include_auto -I./include -g
INT_CPPFLAGS=	
LDFLAGS=	-g
CFLAGS=	-Wall $(CPPFLAGS) -g
CXXFLAGS=	$(CPPFLAGS) -g
CLIBS=	../libvan.a -lpthread -lm
CXXLIBS=	-lpthread 


programs=  imt_tmain log_tmain misc_tmain odt_tmain table_tmain

smalltests=  imtree1

all: $(programs) $(smalltests)

clean:
	rm -fr *.o
	rm -fr *.mach
	rm -fr $(programs) $(smalltests) 

common_objs=  flags_common.o test_common.o van_flags_common.o

db_common_objs=   flags_common.o test_common.o van_flags_common.o

imt_tmain_objs=  imt_t1.o imt_t2.o imt_tmain.o

log_tmain_objs=  log_t1.o log_tmain.o

misc_tmain_objs=  misc_t1.o misc_tmain.o

odt_tmain_objs=  odt_t1.o odt_tmain.o

table_tmain_objs=  table_t1.o table_t2.o table_tmain.o


imtree1_objs= imtree1.o


imt_tmain: $(db_common_objs) $(imt_tmain_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(imt_tmain_objs) $(CLIBS)

log_tmain: $(db_common_objs) $(log_tmain_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(log_tmain_objs) $(CLIBS)

misc_tmain: $(db_common_objs) $(misc_tmain_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(misc_tmain_objs) $(CLIBS)

odt_tmain: $(db_common_objs) $(odt_tmain_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(odt_tmain_objs) $(CLIBS)

table_tmain: $(db_common_objs) $(table_tmain_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(table_tmain_objs) $(CLIBS)

imtree1: $(db_common_objs) $(imtree1_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(imtree1_objs) $(CLIBS)


flags_common.o: ./common/flags_common.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

test_common.o: ./common/test_common.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

van_flags_common.o: ./common/van_flags_common.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

imt_t1.o: ./inmemtree/imt_t1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

imt_t2.o: ./inmemtree/imt_t2.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

imt_tmain.o: ./inmemtree/imt_tmain.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

imtree1.o: ./inmemtree/small/imtree1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

log_t1.o: ./log/log_t1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

log_tmain.o: ./log/log_tmain.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

misc_t1.o: ./misc/misc_t1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

misc_tmain.o: ./misc/misc_tmain.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

odt_t1.o: ./ondisktree/odt_t1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

odt_tmain.o: ./ondisktree/odt_tmain.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

table_t1.o: ./table/table_t1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

table_tmain.o: ./table/table_tmain.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

table_t2.o: ./table/table_t2.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?


