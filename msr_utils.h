#pragma once
#include "job.h"
#define MSR_SAFE_USERSPACE
#include "msr_safe.h"
#undef MSR_SAFE_USERSPACE

void setup_msrsafe_batches( struct job *job );
void teardown_msrsafe_batches( struct job *job );
void populate_allowlist( void );
void dump_batches( struct job *job );
void run_longitudinal_batches( struct job *job, longitudinal_slot_t j );
op_flag_t str2flags( const char * const s );
char* flags2str( op_flag_t flags );
void dprintf_flags( int fd, op_flag_t flags );
void fprintf_flags( FILE* fp, op_flag_t flags );
