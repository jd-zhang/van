# Build the SWIG Python extension that wraps the VanDB engine.
#
# Build libvan.a in the top of the tree first ("make" there), then run
# "make" here.  The result is kvstore.py plus _kvstore.so, both next to
# this Makefile, which is where the server expects to import them from.

CC=		gcc
SWIG=		swig
PYTHON=		python3.11

# The engine lives one level up.
VANDIR=		..
VANLIB=		$(abspath $(VANDIR))/libvan.a

PYINC=		$(shell $(PYTHON) -c "import sysconfig; print(sysconfig.get_paths()['include'])")

CPPFLAGS=	-I$(VANDIR)/include -I$(VANDIR)/include_auto -I$(PYINC) \
		-Iengine -D_REENTRANT -D_GNU_SOURCE
CFLAGS=		-fPIC -g -Wall
LDFLAGS=	-shared

# libvan.a is a shared object despite its name, so the extension has to
# be told where to find it at run time: an absolute path is recorded as
# DT_NEEDED, and rpath covers the case where it is rebuilt in place.
LIBS=		$(VANLIB) -Wl,-rpath,$(dir $(VANLIB)) -lpthread -lm

all: _kvstore.so

$(VANDIR)/libvan.a:
	$(MAKE) -C $(VANDIR) libvan.a

kvstore_wrap.c kvstore.py: engine/kvstore.i
	$(SWIG) -python -o kvstore_wrap.c engine/kvstore.i

kvstore_wrap.o: kvstore_wrap.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ kvstore_wrap.c

engine/kvstore.o: engine/kvstore.c engine/kvstore.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ engine/kvstore.c

_kvstore.so: kvstore_wrap.o engine/kvstore.o $(VANDIR)/libvan.a
	$(CC) $(LDFLAGS) -o $@ kvstore_wrap.o engine/kvstore.o $(LIBS)

clean:
	rm -f kvstore_wrap.c kvstore_wrap.o kvstore.py engine/kvstore.o _kvstore.so
	rm -rf __pycache__ server/__pycache__

.PHONY: all clean
