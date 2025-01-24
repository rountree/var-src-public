#pragma once
#define _GNU_SOURCE     // CPU_SET(3) / <sched.h>
#include <sched.h>
void str2cpuset( const char * const s, cpu_set_t *cpuset );
unsigned int get_next_cpu( unsigned int start_cpu, unsigned int max_cpu, cpu_set_t *cpus );
void print_cpuset( cpu_set_t *cpus );
size_t get_cpuset_count( cpu_set_t *cpuset );
void cpu2cpuset( const int cpu, cpu_set_t * const cpuset );

