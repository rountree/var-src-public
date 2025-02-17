#include <stdint.h>
#include "job.h"

void test_xrstor( void );
void initialize_xrstor( struct benchmark_config *b );
void run_xrstor( struct benchmark_config *b, size_t tid );
void cleanup_xrstor( struct benchmark_config *b );
void dump_xrstor( struct job *job );

