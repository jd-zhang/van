# Created by create_makefile.sh, please do not edit it! 
CC=	gcc
CXX=	g++
CPPFLAGS=	-D_GNU_SOURCE -D_REENTRANT -I.. -I../include -I../include_auto -I./include -g
INT_CPPFLAGS=	
LDFLAGS=	-g
CFLAGS=	-Wall $(CPPFLAGS) -g
CXXFLAGS=	$(CPPFLAGS) -g
CLIBS=	../libvan.a -lpthread 
CXXLIBS=	-lpthread 


programs=  imt_tmain odt_tmain

smalltests=  imtree1

all: $(programs) $(smalltests)

clean:
	rm -fr *.o
	rm -fr *.mach
	rm -fr $(programs) $(smalltests) 

common_objs=  test_common.o

db_common_objs=   test_common.o

imt_tmain_objs=  imt_t1.o imt_tmain.o

odt_tmain_objs=  odt_t1.o odt_tmain.o


imtree1_objs= imtree1.o


imt_tmain: $(db_common_objs) $(imt_tmain_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(imt_tmain_objs) $(CLIBS)

odt_tmain: $(db_common_objs) $(odt_tmain_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(odt_tmain_objs) $(CLIBS)

imtree1: $(db_common_objs) $(imtree1_objs)
	$(CC) -o $@ $(LDFLAGS) $(db_common_objs) $(imtree1_objs) $(CLIBS)


test_common.o: ./common/test_common.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

imt_t1.o: ./inmemtree/imt_t1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

imt_tmain.o: ./inmemtree/imt_tmain.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

imtree1.o: ./inmemtree/small/imtree1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

odt_t1.o: ./ondisktree/odt_t1.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?

odt_tmain.o: ./ondisktree/odt_tmain.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $?


