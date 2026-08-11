#! /bin/sh

exec 3>&1
exec 4>&2
exec 1>Makefile

cflag="$1"

if test "$cflag" = ""; then
	cflag="-g"
fi

loc=".."
inc=""
clib=""

inc="-I$loc -I$loc/include -I$loc/include_auto"
clib="$loc/libvan.a"

ADD_LIBS="-lpthread"

if test "`uname -s`" = "SunOS"; then
	ADD_LIBS="$ADD_LIBS -lsocket -lrt -lnsl"
fi

echo "# Created by create_makefile.sh, please do not edit it! "
echo "CC=	${CC:-gcc}"
echo "CXX=	${CXX:-g++}"
echo "CPPFLAGS=	-D_GNU_SOURCE -D_REENTRANT $inc -I./include $cflag"
echo "INT_CPPFLAGS=	"
echo "LDFLAGS=	$cflag"
echo "CFLAGS=	-Wall \$(CPPFLAGS) $cflag"
echo "CXXFLAGS=	\$(CPPFLAGS) $cflag"
echo "CLIBS=	$clib $ADD_LIBS "
echo "CXXLIBS=	$ADD_LIBS $cxxlib"
echo
echo

# db main files ends with "_tmain.c".
programs=""
while read f; do
	src_file=`basename $f`
	prog_name=`echo $src_file | sed 's/\.c$//g'`
	programs="$programs $prog_name"
done < <(ls */*_tmain.c 2>/dev/null)

# Codecov is special
while read f; do
	src_file=`basename $f`
	prog_name=`echo $src_file | sed 's/\.c$//g'`
	programs="$programs $prog_name"
done < <(find codecov -name "*_tmain.c" 2>/dev/null)
echo "programs= $programs"
echo

# db small tests.
smalltests=""
while read f;do
	src_file=`basename $f`
	prog_name=`echo $src_file | sed 's/\.c$//g'`
	smalltests="$smalltests $prog_name"
done < <(ls */small/*.c 2>/dev/null)
echo "smalltests= $smalltests"
echo

echo "all: \$(programs) \$(smalltests)"
echo

echo "clean:"
echo "	rm -fr *.o"
echo "	rm -fr *.mach"
echo "	rm -fr \$(programs) \$(smalltests) "
echo

# c common objects
common_objs=""
while read f; do
	src_file=`basename $f`
	obj_name=`echo $src_file | sed 's/\.c$/.o/g'`
	common_objs="$common_objs $obj_name"
done < <(ls common/*.c 2>/dev/null)
echo "common_objs= $common_objs"
echo

# db common objects
db_common_objs=""
while read f; do
	src_file=`basename $f`
	obj_name=`echo $src_file | sed 's/\.c$/.o/g'`
	db_common_objs="$db_common_objs $obj_name"
done < <(ls common/db/*.c 2>/dev/null)
echo "db_common_objs= $db_common_objs $common_objs"
echo

for prog in $programs;do
	tmp_objs=""
	main_file=`find . -name $prog.c`
	dir_name=`dirname $main_file`
	while read f;do
		src_file=`basename $f`
		obj_name=`echo $src_file | sed 's/\.c$/.o/g'`
		tmp_objs="$tmp_objs $obj_name"
	done< <(ls $dir_name/*.c 2>/dev/null)
	echo "${prog}_objs= $tmp_objs"
	echo
done
echo

while read f;do
	src_file=`basename $f`
	prog_name=`echo $src_file | sed 's/\.c$//g'`
	echo "${prog_name}_objs= ${prog_name}.o"
	echo
done < <(ls */small/*.c 2>/dev/null)
echo

for prog in $programs $smalltests;do
	echo "$prog: \$(db_common_objs) \$(${prog}_objs)"
	echo "	\$(CC) -o \$@ \$(LDFLAGS) \$(db_common_objs) \$(${prog}_objs) \$(CLIBS)"
	echo
done
echo

while read f;do
	src_file=`basename $f`
	obj_name=`echo $src_file | sed 's/\.c$/.o/g'`
	echo "$obj_name: $f"
	if echo $f | grep -i "internal" >/dev/null 2>/dev/null; then
		echo "	\$(CC) -c -o \$@ \$(INT_CPPFLAGS) \$(CPPFLAGS) \$(CFLAGS) \$?"
	else
		echo "	\$(CC) -c -o \$@ \$(CPPFLAGS) \$(CFLAGS) \$?"
	fi
	echo
done < <(find . -name "*.c")
echo

# while read f;do
#	src_file=`basename $f`
#	obj_name=`echo $src_file | sed 's/\.c$/.o/g'`
#	echo "$obj_name: $f"
#	if echo $f | grep -i "internal" >/dev/null 2>/dev/null; then
#		echo "	\$(CC) -c -o \$@ \$(INT_CPPFLAGS) \$(CPPFLAGS) \$(CFLAGS) \$?"
#	else
#		echo "	\$(CC) -c -o \$@ \$(CPPFLAGS) \$(CFLAGS) \$?"
#	fi
#	echo
#done < <(ls */small/*.c)
#echo

exec 1>&3
exec 2>&4


