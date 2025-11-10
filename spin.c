/* spin.c */
#define _XOPEN_SOURCE 500 // for random(3)
#define _POSIX_C_SOURCE 200112L // for posix_memalign(3)
#include <assert.h>
#include <stdlib.h>     // posix_memalign(3), random(3)
#include <stdio.h>
#include <unistd.h>     // sysconf(3)
#include "spin.h"
void run_spin( struct benchmark_config *b ){
    uint64_t accumulator = 0;
    for( ; ! (*(b->halt)); accumulator++ );
    b->executed_loops[ 0 ] += accumulator;
}

void run_abshift( struct benchmark_config *b ){

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

#define NR (size_t)(1024ull * 1024ull * 1024ull )
static uint64_t *R; // Shared across all abxor threads.

void setup_abxor( void ){
    // Allocate page-aligned space for 1B uint64_t.
    fprintf( stderr, "Starting random number generation...\n" );
    assert( 0 == posix_memalign( (void**)(&R), sysconf(_SC_PAGESIZE), NR * sizeof(uint64_t) ) );
    for( size_t i = 0; i < NR; i++ ){
        R[i] = random();
    }
    fprintf( stderr, "Random number generation complete.\n");
}

/*
void run_abxor( struct benchmark_config *b ){

}
*/
