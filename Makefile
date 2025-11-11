# Required versions
#
# gcc 14+
# 	Required for -std=c23.

# Power lab machines
ifneq ($(filter $(shell hostname | sed -e "s/[0-9]//"), comus octomore rhetoric iris lupine gilia sorrel curry thompson),)
	CFLAGS=-iquote /dev/shm/msr-safe -std=c23
	CC=${HOME}/install/gcc-15.2.0/bin/gcc
# rz machines
else ifneq ($(filter $(shell hostname | sed -e "s/[0-9]//"), rzhound rzwhippet),)
	CFLAGS=-iquote /usr/workspace/msr-adm/rzhound_dat/msr-safe -std=c2x
	CC=gcc
# cz machines
else ifneq ($(filter $(shell hostname | sed -e "s/[0-9]//"), dane poodle),)
	CFLAGS=-iquote /p/vast1/rountree/poodle/repos/msr-safe -std=c23
	CC=/p/vast1/rountree/poodle/install/gcc-trunk-27Oct2024/bin/gcc
# offsite machines
else ifneq ($(filter $(shell hostname | sed -e "s/[0-9]//"), serif catharsis),)
	CFLAGS=-iquote ${HOME}/repos/msr-safe -std=c23
	CC=gcc
# generic option
else
	CFLAGS=-iquote ${HOME}/repos/msr-safe -std=c23
	CC=gcc
endif

# Common
CFLAGS+=-Wall -Wextra -march=native -mxsave -fdiagnostics-color=always
CFLAGS+=-Werror
LDFLAGS=-lpthread

# Debugging
CFLAGS+=-D_FORTIFY_SOURCE=3 -g -Og

# Production
#CFLAGS+=-O2

vanallin: Makefile cpuset_utils.o msr_utils.o options.o spin.o main.o timespec_utils.o int_utils.o
	$(CC) $(LDFLAGS) cpuset_utils.o msr_utils.o options.o spin.o main.o timespec_utils.o int_utils.o -o var

clean:
	rm -f *.o var

