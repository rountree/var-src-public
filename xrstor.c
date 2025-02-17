#include <cpuid.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>     // exit(3), posix_memalign(3)
#include <immintrin.h>  // _xgetbv(intrinsic), _xsave64(intrinsic), xrstor64(intrinsic), _mm256_zeroall(intrinsic)
#include <stdint.h>
#include <inttypes.h>
#include <string.h>	// memcpy(3), memset(3)
#include <sys/time.h>
#include "xrstor.h"
#include "job.h"

constexpr const size_t NUM_REGIONS=256;
constexpr const uint8_t XSAVE_MASK = 0x07;  // 0x1=x87, 0x2=sse, 0x4=avx.
constexpr const size_t NUM_YMMx_LO_BYTES = 256;
constexpr const size_t NUM_YMMx_HI_BYTES = 256;
constexpr const size_t YMMx_LO_BYTES_OFFSET = 160;
constexpr const size_t YMMx_HI_BYTES_OFFSET = 576;
constexpr const size_t XSAVE_MASK_OFFSET = 512;
constexpr const size_t REGION_SIZE = 848;
constexpr const size_t PAGE_ALIGNMENT = 4096;

void
initialize_xrstor( struct benchmark_config *b ){

    // Allocate xrstor region using aligned memory
    assert( 0 == posix_memalign( (void**)(&b->benchmark_addr), PAGE_ALIGNMENT, REGION_SIZE ) );

    // Get a known-good layout
    _xsave64( b->benchmark_addr, XSAVE_MASK );

    // Populate registers in xrstor region with fill value
    for( size_t i = 0; i < (256/8); i++ ){
        ((uint64_t*)(&b->benchmark_addr[ YMMx_LO_BYTES_OFFSET ]))[ i ] = b->benchmark_param1;
        ((uint64_t*)(&b->benchmark_addr[ YMMx_HI_BYTES_OFFSET ]))[ i ] = b->benchmark_param2;
    }
}

void
test_xrstor( void ){
    /* The initial version of this code set up 256 restore regions, each filled with a
     * unique byte value.  This arrangement allowed for using different values in the
     * same run without having to stop and repopulate a new region.  Eight bits turned
     * out to be slightly less granularity than we needed.  Keep this around for testing
     * purposes (although it is massive overkill).
     */
    static uint8_t *xrstor_region[NUM_REGIONS];

    // Step 1:  does the processor support the xsave, xrstor, and xgetbv instructions?
    unsigned int eax, ebx, ecx, edx;
    assert( 1 == __get_cpuid( 0x01, &eax, &ebx, &ecx, &edx ) );
    if( 0 == (ecx & (1 << 26)) ){
        fprintf( stderr, "cpuid leaf 0x01 ecx[26] is 0, which means the xsave instruction is not supported on this processor.  Bye!\n");
        exit( -1 );
    }

    // Step 2:  does the processor + OS allow AVX state to be saved and restored?
    uint64_t bv = _xgetbv(0);
    if( 0 == (bv & (1 << 2)) ){
        fprintf( stderr, "The xgetbv instruction reports that either the cpu does not support or the OS does not allow save/restore of AVX registers.  Bye!\n");
        exit( -1 );
    }

    // Step 3:  allocate memory for the test region (must be on 64-byte boundary, we'll do 4k for luck).

    uint8_t *test_region;
    assert( 0 == posix_memalign( (void**)(&  test_region), PAGE_ALIGNMENT, REGION_SIZE ) );

    // Step 4:  set up 256 different fill regions.
    for( size_t fill = 0; fill < NUM_REGIONS; fill++ ){
        // Step 4a:  Memory allocation with page alignment.
        assert( 0 == posix_memalign( (void**)(&xrstor_region[fill]), PAGE_ALIGNMENT, REGION_SIZE ) );

        // Step 4b:  Get a known-good layout.
        // The layouts are as follows:
        //      bytes 000-031:  Legacy x87 and sse headers
        //            032-159:  8 10-byte x87 registers and 8 6-byte reserved areas
        //            160-415:  (256 bytes) 16 16-byte/128-bit sse registers OR the low 16 bytes of 16 32-byte/256-bit avx registers
        //            416-463:  Reserved
        //            464-511:  Unused
        //            512-519:  XSTATE_BV Set byte 512 to 0x7 to only include x87 state, sse state, and avx state.
        //            520-527:  XCOMP_BV  Ensure the high bit of byte 527 is 0 to prevent compression (XCOMP_BV[63])
        //            528-575:  Reserved
        //            576-831:  (256 bytes) high 16 bytes of 16 32-byte/256-bit avx registers
        //            832-847:  (16  bytes) fencing to detect shenanigans.
        _xsave64( xrstor_region[fill], XSAVE_MASK );

        // Step 4c:  Force XSTATE_BV to include avx.
        xrstor_region[fill][XSAVE_MASK_OFFSET] = XSAVE_MASK;

        // Step 4d:  Populate "saved" register values with fill values.
        memset( &(xrstor_region[fill][YMMx_LO_BYTES_OFFSET]), fill, NUM_YMMx_LO_BYTES); // XMMO-XMM15 low 64 bytes
        memset( &(xrstor_region[fill][YMMx_HI_BYTES_OFFSET]), fill, NUM_YMMx_HI_BYTES); // YMM0-YMM15 high 64 bytes

        // Step 4e:  Restore the modified region (i.e. load the modified values into the registers).
        _xrstor64( xrstor_region[fill], XSAVE_MASK );

        // Step 4f:  Save the registers to the test region so we can make sure all went well.
        _xsave64( test_region, XSAVE_MASK );

        // Step 4g:  Walk throught the register values looking for issues.
        for( size_t i=YMMx_LO_BYTES_OFFSET; i<(YMMx_LO_BYTES_OFFSET+NUM_YMMx_LO_BYTES); i++ ){
            if( test_region[i] != fill ){
                fprintf(stderr, "test_region[%zu] = %"PRIu8".  Expected value was %zu.  Initialization of xrstor region failed.  Bye!\n",
                        i, test_region[i], fill);
                exit( -1 );
            }
        }
        for( size_t i=YMMx_HI_BYTES_OFFSET; i<(YMMx_HI_BYTES_OFFSET+NUM_YMMx_HI_BYTES); i++ ){
            if( test_region[i] != fill ){
                fprintf(stderr, "test_region[%zu] = %"PRIu8".  Expected value was %zu.  initialization of xrstor region failed.  Bye!\n",
                        i, test_region[i], fill);
                exit( -1 );
            }
        }
    }
    // Step 5:  Cleanup
    free( test_region );
    for( size_t i=0; i < NUM_REGIONS; i++ ){
        free( xrstor_region[i] );
    }
}



void
run_xrstor( struct benchmark_config *b, size_t tid ){
    uint64_t accumulator = 0;
    // FIXME not thread safe
    while( ! (*(b->halt)) ){
        _mm256_zeroall(); 		                        // Zeros YMM0-YMM15.
        _xrstor64( b->benchmark_addr, XSAVE_MASK );     // Flash YMM0-YMM15
        accumulator++;
    }
    b->executed_loops[0][tid] += accumulator;
}

void
cleanup_xrstor( struct benchmark_config *b ){
    free( b->benchmark_addr );
}

void
dump_xrstor( struct job *job ){

    // FIXME need to iterate over threads
    for( size_t i = 0; i < job->benchmark_count; i++ ){
        for( size_t thread_idx = 0; thread_idx < job->benchmarks[i]->thread_count; thread_idx++ ){
            printf("# Benchmark %zu thread %zu had %"PRIu64" executions.\n", i, thread_idx, job->benchmarks[i]->executed_loops[ 0 ][ thread_idx ] );
        }
    }
}

