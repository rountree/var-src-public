#pragma once
#define _GNU_SOURCE     // CPU_SET(3) / <sched.h>
#include <sched.h>
void str2cpuset( const char * const s, cpu_set_t *cpuset );
char* cpuset2str( cpu_set_t *cpuset );
unsigned int get_next_cpu( unsigned int start_cpu, unsigned int max_cpu, cpu_set_t *cpus );
void dprintf_cpuset( int fd, cpu_set_t *cpus );
void fprintf_cpuset( FILE *fp, cpu_set_t *cpus );
size_t get_cpuset_count( cpu_set_t *cpuset );
void cpu2cpuset( int cpu, cpu_set_t * const cpuset );

