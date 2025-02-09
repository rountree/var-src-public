/* spin.c */
#include "spin.h"
void run_spin( struct benchmark_config *b ){
    uint64_t accumulator = 0;
    uint64_t to_be_shifted = b->benchmark_param1;

    for( ; ! (*(b->halt)) && to_be_shifted; accumulator++ ){
        to_be_shifted  = accumulator % 2 ? to_be_shifted << 1 : to_be_shifted >> 1;
    }
    b->executed_loops = accumulator;
    /*
    uint64_t l = b->benchmark_param1;
    uint64_t r = b->benchmark_param2;
    while( ! (*(b->halt))){
        b->executed_loops++;
        b->benchmark_param1 = l ^ r;
    }
    */
}
