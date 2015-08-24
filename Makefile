CC=	gcc
CFLAGS=		-c -g $(OUTCFLAGS)
LDFLAGS=	-g -Wall $(OUTCFLAGS)
CPPFLAGS=	-I./include -I./include_auto -D_REENTRANT -D_GNU_SOURCE
CXX=	g++
CXXFLAGS=	$(CPPFLAGS) -g $(OUTCFLAGS)
CLIBS=		-lpthread -L/usr/local/lib
CXXLIBS=	-lpthread -L/usr/local/lib
#CLIBS=		-lpthread -L/usr/local/lib -ltcmalloc_minimal
#CXXLIBS=	-lpthread -L/usr/local/lib -ltcmalloc_minimal


all: libvan.a
test: all
	cp -f libvan.a test && cd test && make
# Libraries.
VAN_OBJS=\
	 van_alloc.o van_alloc_debug.o van_io.o van_hash.o \
	 van_hash_city.o van_hash_fnv.o van_syserr.o \
	 datum.o \
	 inmemtree.o inmemtree_compare.o inmemtree_config.o \
	 inmemtree_cursor.o inmemtree_debug.o inmemtree_init.o \
	 inmemtree_node.o inmemtree_ret.o inmemtree_search.o \
	 inmemtree_split.o inmemtree_stat.o inmemtree_traverse.o \
	 ondisktree.o ondisktree_build.o ondisktree_cache.o \
	 mutex_debug.o \
	 ondisktree_compare.o ondisktree_cursor.o ondisktree_debug.o \
	 ondisktree_item.o ondisktree_ret.o ondisktree_search.o \
	 van_store.o \
	 van_table.o

	 

libvan.a: $(VAN_OBJS)
	$(CC) -shared -o $@ $(VAN_OBJS) $(CLIBS)

clean:
	cd test && make clean && rm -f libvan.a
	rm -fr *.o
	rm -fr *.a

# Object files.
# src/common
van_alloc.o: src/common/van_alloc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
van_alloc_debug.o: src/common/van_alloc_debug.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
van_hash.o: src/common/van_hash.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
van_hash_city.o: src/common/van_hash_city.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
van_hash_fnv.o: src/common/van_hash_fnv.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
van_io.o: src/common/van_io.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
van_syserr.o: src/common/van_syserr.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
# src/datum
datum.o: src/datum/datum.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?

# src/inmemtree
inmemtree.o: src/inmemtree/inmemtree.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_config.o: src/inmemtree/inmemtree_config.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_compare.o: src/inmemtree/inmemtree_compare.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_cursor.o: src/inmemtree/inmemtree_cursor.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_debug.o: src/inmemtree/inmemtree_debug.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_init.o: src/inmemtree/inmemtree_init.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_node.o: src/inmemtree/inmemtree_node.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_ret.o: src/inmemtree/inmemtree_ret.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_search.o: src/inmemtree/inmemtree_search.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_split.o: src/inmemtree/inmemtree_split.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_stat.o: src/inmemtree/inmemtree_stat.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
inmemtree_traverse.o: src/inmemtree/inmemtree_traverse.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?

# src/mutex
mutex_debug.o: src/mutex/mutex_debug.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?

# src/ondisktree
ondisktree.o: src/ondisktree/ondisktree.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_build.o: src/ondisktree/ondisktree_build.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_cache.o: src/ondisktree/ondisktree_cache.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_compare.o: src/ondisktree/ondisktree_compare.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_cursor.o: src/ondisktree/ondisktree_cursor.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_debug.o: src/ondisktree/ondisktree_debug.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_item.o: src/ondisktree/ondisktree_item.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_ret.o: src/ondisktree/ondisktree_ret.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
ondisktree_search.o: src/ondisktree/ondisktree_search.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?

# src/store
van_store.o: src/store/van_store.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?

# src/table
van_table.o: src/table/van_table.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
