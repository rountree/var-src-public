/* spin.c */
#include "spin.h"
void run_spin( struct benchmark_config *b ){
    uint64_t accumulator = 0;
    for( ; ! (*(b->halt)); accumulator++ );

    // FIXME This should have a mutex, but I doubt it matters.
    b->executed_loops += accumulator;
}

void run_abxor( struct benchmark_config * b){

    uint64_t accumulator = 0;
    uint64_t to_be_shifted[2] = { b->benchmark_param1, b->benchmark_param2 };
    uint64_t shift_amount     = b->benchmark_param3;

    for( ; ! (*(b->halt)); accumulator++ ){
        bool idx = b->ab_selector;
        to_be_shifted[ idx ] = ( to_be_shifted[ idx ] << shift_amount ) >> shift_amount;
    }
    b->benchmark_param1 = to_be_shifted[ 0 ];
    b->benchmark_param2 = to_be_shifted[ 1 ];
    // FIXME This should have a mutex, but I doubt it matters.
    b->executed_loops += accumulator;
}
