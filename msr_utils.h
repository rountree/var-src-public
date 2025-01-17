#pragma once
#include "job.h"

void setup_msrsafe_batches( struct job *job );
void teardown_msrsafe_batches( struct job *job );
void populate_allowlist( void );
void dump_batches( struct job *job );
void run_longitudinal_batches( struct job *job, longitudinal_batch_t j );
