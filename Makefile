# Required versions
#
# glibc 3.34+
# 	Used for debugging with _FORTIFY_SOURCE=3
#	Execute the library to get version information, e.g,
#		/lib/x86_64-linux-gnu/libc.so.6
#
# gcc 14+
# 	Required for -std=c23.

# Serif
CFLAGS=-iquote ${HOME}/repos/msr-safe
CC=gcc-14

# Poodle
#CFLAGS=-iquote /p/vast1/rountree/poodle/repos/msr-safe
#CC=/p/vast1/rountree/poodle/install/gcc-trunk-27Oct2024/bin/gcc

# Gilia
#CFLAGS=-iquote ${HOME}/gilia/repos/msr-safe
#CC=${HOME}/gilia/install/gcc-trunk-11Nov2024/bin/gcc

# Common
CFLAGS+=-Wall -Wextra -march=native -mxsave -std=c23 -fdiagnostics-color=always
CFLAGS+=-Werror
LDFLAGS=-lpthread

# Debugging
#CFLAGS+=-D_FORTIFY_SOURCE=3 -g -Og

# Production
CFLAGS+=-O2

vanallin: Makefile cpuset_utils.o msr_utils.o options.o spin.o main.o timespec_utils.o int_utils.o
	$(CC) $(LDFLAGS) cpuset_utils.o msr_utils.o options.o spin.o main.o timespec_utils.o int_utils.o -o var

clean:
	rm -f *.o var

