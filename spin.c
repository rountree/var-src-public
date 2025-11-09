/* spin.c */
#include "spin.h"
void run_spin( struct benchmark_config *b ){
    uint64_t accumulator = 0;
    for( ; ! (*(b->halt)); accumulator++ );
    b->executed_loops[ 0 ] += accumulator;
}

void run_abxor( struct benchmark_config *b ){

    // If you touch this code, make sure to check to see if the compiler
    // optimized away the actual shift instructions.  Some of what's going
    // on here is relatively subtle.
    uint64_t accumulator[2] = {};
    uint64_t to_be_shifted[2] = { b->benchmark_param1, b->benchmark_param2 };
    uint64_t shift_amount     = b->benchmark_param3;

    for( ; ! (*(b->halt)); accumulator[*(b->ab_selector)]++ ){
        bool idx = *(b->ab_selector);
        to_be_shifted[ idx ] = ( to_be_shifted[ idx ] << shift_amount ) >> shift_amount;
    }
    b->benchmark_param1 = to_be_shifted[ 0 ];   // forces the shifts to be executed, as
    b->benchmark_param2 = to_be_shifted[ 1 ];   // the results are externally visible.
    b->executed_loops[ 0 ] += accumulator[ 0 ];
    b->executed_loops[ 1 ] += accumulator[ 1 ];
}

/*
void run_abxor( struct benchmark_config *b ){

}
*/
