#define _GNU_SOURCE         // for sched.h
#include <stdlib.h>         // calloc(3)
#include <string.h>         // memcpy(3)
#include <assert.h>         // assert(3)
#include <fcntl.h>          // open(2)
#include <unistd.h>         // write(2), close(2)
#include <stdint.h>         // uint64_t etc.
#include <inttypes.h>       // PRIu64 etc.
#include <stdio.h>          // printf(3), perror(3)
#include <sys/ioctl.h>      // ioctl(2)
#include <errno.h>          // errno
#include "msr_safe.h"       // struct msr_batch_array, struct msr_batch_op, X86_IOC_MSR_BATCH
#include "msr_version.h"    // MSR_SAFE_VERSION_u32
#include "cpuset_utils.h"   // get_next_cpu()
#include "msr_utils.h"


static uint16_t max_msrsafe_cpu = UINT16_MAX;   // current limitation of msr-safe.

//////////////////////////////////////////////////////////////////////////////////
// List of MSRs
//////////////////////////////////////////////////////////////////////////////////

static constexpr const uint32_t PERF_STATUS             = 0x0198;
static constexpr const uint32_t THERM_STATUS            = 0x019C;  // 22:16 Degrees C away from max

static constexpr const uint32_t PKG_ENERGY_STATUS       = 0x0611;

static constexpr const uint32_t PERF_GLOBAL_CTRL        = 0x038F;
static constexpr const uint32_t FIXED_CTR_CTRL          = 0x038D;
static constexpr const uint32_t FIXED_CTR0              = 0x0309;   // INST_RETIRED.ANY
static constexpr const uint32_t FIXED_CTR1              = 0x030A;   // CPU_CLK_UNHALTED.[THREAD|CORE]
static constexpr const uint32_t FIXED_CTR2              = 0x030B;   // CPU_CLK_UNHALTED.REF_TSC

static const char * const msr2str[] = {
    [PERF_STATUS]       = "PERF_STATUS",
    [THERM_STATUS]      = "THERM_STATUS",
    [PKG_ENERGY_STATUS] = "PKG_ENERGY_STATUS",
    [PERF_GLOBAL_CTRL]  = "PERF_GLOBAL_CTRL",
    [FIXED_CTR_CTRL]    = "FIXED_CTR_CTRL",
    [FIXED_CTR0]        = "FIXED_CTR0",
    [FIXED_CTR1]        = "FIXED_CTR1",
    [FIXED_CTR2]        = "FIXED_CTR2"
};

static const char * const op2str[] = {
    [OP_WRITE]          = "WRITE",
    [OP_READ]           = "READ",
    [OP_POLL]           = "POLL",
    [0x08]              = "UNUSED_0x08",
    [OP_TSC_INITIAL]    = "TSC_INITIAL",
    [OP_TSC_POLL]       = "TSC_POLL",
    [OP_TSC_FINAL]      = "TSC_FINAL",
    [0x80]              = "UNUSED_0x80",
    [OP_THERM_INITIAL]  = "THERM_INITIAL",
    [OP_THERM_FINAL]    = "THERM_FINAL"
};

//////////////////////////////////////////////////////////////////////////////////
// Allowlist.  Order by most-used to least-used.
//////////////////////////////////////////////////////////////////////////////////

static char const *const allowlist = "0x0611 0x0000000000000000\n"      // PKG_ENERGY_STATUS
                                     "0x0198 0x0000000000000000\n"      // PERF_STATUS
                                     "0x019C 0x0000000000000000\n"      // THERM_STATUS
                                     "0x0010 0x0000000000000000\n"      // TIME_STAMP_COUNTER
                                     "0x0309 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR0
                                     "0x030A 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR1
                                     "0x030B 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR2
                                     "0x030C 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR3
                                     "0x038D 0x0000000000000333\n"      // FIXED_CTR_CTRL
                                     "0x038F 0x0000000700000000\n"      // PERF_GLOBAL_CTRL
                                     ;

static constexpr const uint32_t MAX_POLL_ATTEMPTS = 10000;

//////////////////////////////////////////////////////////////////////////////////
// Available ops.
//////////////////////////////////////////////////////////////////////////////////
//                  u16 u16 s32 u32      u32 u64     u64   u64 u64 u64 u64 u64 u64 u64
//                  cpu op  err poll_max msr msrdata wmask mi  mp  mf  ai  ap  af  msrdata2

static constexpr const struct msr_batch_op op_zero_fixed_ctr0 = { .op = OP_WRITE | OP_TSC_INITIAL, .msr = FIXED_CTR0 };
static constexpr const struct msr_batch_op op_zero_fixed_ctr1 = { .op = OP_WRITE | OP_TSC_INITIAL, .msr = FIXED_CTR1 };
static constexpr const struct msr_batch_op op_zero_fixed_ctr2 = { .op = OP_WRITE | OP_TSC_INITIAL, .msr = FIXED_CTR2 };

static constexpr const struct msr_batch_op op_read_fixed_ctr0 = { .op = OP_READ | OP_TSC_INITIAL, .msr = FIXED_CTR0 };
static constexpr const struct msr_batch_op op_read_fixed_ctr1 = { .op = OP_READ | OP_TSC_INITIAL, .msr = FIXED_CTR1 };
static constexpr const struct msr_batch_op op_read_fixed_ctr2 = { .op = OP_READ | OP_TSC_INITIAL, .msr = FIXED_CTR2 };

// Enable all three fixed-function performance counters, non-global
static constexpr const struct msr_batch_op op_enable_fixed = { .op = OP_WRITE | OP_TSC_INITIAL, .msr = FIXED_CTR_CTRL, .msrdata=0x333 };

// Enable/disable all fixed and programmable counters, global.  (Just fixed counters for now.)
static constexpr const struct msr_batch_op op_start_global = { .op = OP_WRITE | OP_TSC_INITIAL, .msr = PERF_GLOBAL_CTRL, .msrdata=0x700000000 };
static constexpr const struct msr_batch_op op_stop_global  = { .op = OP_WRITE | OP_TSC_INITIAL, .msr = PERF_GLOBAL_CTRL, .msrdata=0x000000000 };

static constexpr const struct msr_batch_op op_poll_pkg_J = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL | OP_THERM_INITIAL | OP_THERM_FINAL, .msr = PKG_ENERGY_STATUS, .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_pkg_F = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL, .msr = PERF_STATUS,       .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_pkg_C = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL, .msr = THERM_STATUS,      .poll_max=MAX_POLL_ATTEMPTS };

void teardown_msrsafe_batches( struct job *job ){

    // Polls are easy.
    for( size_t i = 0; i < job->poll_count; i++ ){
        free( job->polls[i]->poll_batches );
        free( job->polls[i]->poll_ops );
    }
    // Longitudinals are a little tricker.
    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        for( longitudinal_batch_t j = 0; j < NUM_LONGITUDINAL_BATCH_TYPES; j++ ){
            for( size_t k = 0; k < job->longitudinals[i]->longitudinal_batch_count_per_type[ j ]; k++ ){
                free( job->longitudinals[i]->longitudinal_batches[j][k].ops );
            }
            free( job->longitudinals[i]->longitudinal_batches[j] );
        }
    }
}

void setup_msrsafe_batches( struct job *job ){

    // Map the polling batches
    for( size_t i = 0; i < job->poll_count; i++ ){
        // Assume msrs being polled will be updated 1k times/second.
        job->polls[i]->total_ops = 1024 * job->duration.tv_sec;
        job->polls[i]->poll_batches = calloc( job->polls[i]->total_ops, sizeof( struct msr_batch_array ) );
        assert( job->polls[i]->poll_batches );
        job->polls[i]->poll_ops = calloc( job->polls[i]->total_ops, sizeof( struct msr_batch_op ) );
        assert( job->polls[i]->poll_ops );

        // Leave the last op as all-zeros.
        const struct msr_batch_op * op;
        switch( job->polls[i]->poll_type ){
            case POWER:
                op = &op_poll_pkg_J;
                break;
            case THERMAL:
                op = &op_poll_pkg_C;
                break;
            case FREQUENCY:
                op = &op_poll_pkg_F;
                break;
            default:
                assert(0);
                break;
        }

        // Find the polled cpu.
        uint16_t polled_cpu = (uint16_t)( get_next_cpu( 0, 255, &(job->polls[i]->polled_cpu) ) );

        // For each op, fill in an msr_batch_array and a single msr_batch_op.
        for( size_t j = 0; j < job->polls[i]->total_ops; j++ ){
            job->polls[i]->poll_batches[j].numops = 1;
            job->polls[i]->poll_batches[j].version = MSR_SAFE_VERSION_u32;
            job->polls[i]->poll_batches[j].ops = &(job->polls[i]->poll_ops[j]);
            memcpy( &(job->polls[i]->poll_ops[j]), op, sizeof( struct msr_batch_op ) );
            job->polls[i]->poll_ops[j].cpu = polled_cpu;
            job->polls[i]->poll_ops[j].err = (uint32_t)(0xDECAFBAD);
       }
    }

    // Map the longitudinal batches
    // It's possible to stuff everything into a single batch, but it's easier to reason
    // about multiple batches, each doing one thing on multiple CPUs.
    for( size_t i = 0; i < job->longitudinal_count; i++ ){

        uint32_t ncpu = CPU_COUNT( &(job->longitudinals[i]->sample_cpus) );

        if( FIXED_FUNCTION_COUNTERS == job->longitudinals[i]->longitudinal_type ){

            job->longitudinals[i]->longitudinal_batch_count_per_type[0] = 5;// 5 batches for SETUP  ( global stop, zero x3, local enable ).
            job->longitudinals[i]->longitudinal_batch_count_per_type[1] = 1;// 1 batch   for START  ( global start ).
            job->longitudinals[i]->longitudinal_batch_count_per_type[2] = 1;// 1 batch   for STOP   ( global stop  ).
            job->longitudinals[i]->longitudinal_batch_count_per_type[3] = 3;// 3 batches for READ   ( read x3 ).
            job->longitudinals[i]->longitudinal_batch_count_per_type[4] = 0;// 0 batches for TEARDOWN

            for( longitudinal_batch_t j = 0; j < NUM_LONGITUDINAL_BATCH_TYPES; j++ ){

                job->longitudinals[i]->longitudinal_batches[j] =
                    calloc( job->longitudinals[i]->longitudinal_batch_count_per_type[j], sizeof( struct msr_batch_array ) );
                assert( job->longitudinals[i]->longitudinal_batches[j] );
                for( size_t k = 0; k < job->longitudinals[i]->longitudinal_batch_count_per_type[j]; k++ ){
                    job->longitudinals[i]->longitudinal_batches[j][k].numops = ncpu;
                    job->longitudinals[i]->longitudinal_batches[j][k].version = MSR_SAFE_VERSION_u32;
                    job->longitudinals[i]->longitudinal_batches[j][k].ops =
                        calloc( ncpu, sizeof( struct msr_batch_op ) );
                    assert( job->longitudinals[i]->longitudinal_batches[j][k].ops );

                    // Figure out which op we need.
                    const struct msr_batch_op *op;
                    switch( j ){
                        case SETUP:{
                                       switch( k ){
                                           case 0:  op = &op_stop_global;       break;
                                           case 1:  op = &op_zero_fixed_ctr0;   break;
                                           case 2:  op = &op_zero_fixed_ctr1;   break;
                                           case 3:  op = &op_zero_fixed_ctr2;   break;
                                           case 4:  op = &op_enable_fixed;      break;
                                           default: assert(0);                  break;
                                       }
                                       break;
                                   }
                        case START:                 op = &op_start_global;      break;
                        case STOP:                  op = &op_stop_global;       break;
                        case READ:{
                                      switch( k ){
                                          case 0:   op = &op_read_fixed_ctr0;   break;
                                          case 1:   op = &op_read_fixed_ctr1;   break;
                                          case 2:   op = &op_read_fixed_ctr2;   break;
                                          default:  assert(0);                  break;
                                      }
                                      break;
                                  }
                        default: assert(0); break;
                    };

                    uint16_t cpu_idx = 0;
                    for( size_t c = 0; c < ncpu; c++ ){
                        memcpy( &(job->longitudinals[i]->longitudinal_batches[j][k].ops[c]), op, sizeof( struct msr_batch_op ) );
                        cpu_idx = get_next_cpu( cpu_idx, max_msrsafe_cpu, &(job->longitudinals[i]->sample_cpus) );
                        job->longitudinals[i]->longitudinal_batches[j][k].ops[c].cpu = cpu_idx;
                        job->longitudinals[i]->longitudinal_batches[j][k].ops[c].err = (int32_t)(0xDECAFBAD);
                        cpu_idx++;
                    }
                } // end k-loop over longitudinal_batch_count_per_type[j]
            } // end j-loop over NUM_LONGITUDINAL_BATCH_TYPES

        }else{
            assert(0); // no other longitudinal tasks types to chooose from.
        }
    }
}

void populate_allowlist( void ) {
    // Keep it manual.
    int fd = open("/dev/cpu/msr_allowlist", O_WRONLY);
    assert(-1 != fd);
    ssize_t nbytes = write(fd, allowlist, strlen(allowlist));
    assert(strlen(allowlist) == nbytes);
    close(fd);
}


static void print_header( void ){
    printf("cpu "
            "op "
            "err "
            "poll_max "
            "msr "
            "msrdata "
            "wmask "
            "tsc_initial "
            "tsc_poll "
            "tsc_final "
            "msrdata2 "
            "therm_initial "
            "therm_final "
            "\n");

    printf("#c "
            "o "
            "e "
            "pm "
            "msr "
            "val "
            "wmask "
            "tsci "
            "tscp "
            "tscf "
            "val2 "
            "ti "
            "tf "
            "\n");
}

static void print_op( struct msr_batch_op *o ){

    // All this can be stuffed into a single printf(), and I have done that several times, and
    // it's a pain to debug.  Doubt this will be noticably slower.
    printf("%#"PRIx16" ", (uint16_t)o->cpu);
    printf("%#"PRIx16" ", (uint16_t)o->op);
    printf("%#"PRIx32" ", ( int32_t)o->err);
    printf("%#"PRIx32" ", (uint32_t)o->poll_max);
    printf("%#"PRIx32" ", (uint32_t)o->msr);
    printf("%#012"PRIx64" ", (uint64_t)o->msrdata);
    printf("%#"PRIx64" ", (uint64_t)o->wmask);
    printf("%#"PRIx64" ", (uint64_t)o->tsc_initial);
    printf("%#"PRIx64" ", (uint64_t)o->tsc_poll);
    printf("%#"PRIx64" ", (uint64_t)o->tsc_final);
    printf("%#"PRIx64" ", (uint64_t)o->msrdata2);
    printf("%#"PRIx64" ", (uint64_t)o->therm_initial);
    printf("%#"PRIx64" ", (uint64_t)o->therm_final);

    printf("# ");

    printf("%s ", msr2str[ o->msr ]);
    uint16_t op = o->op;
    for( size_t op_idx = 0; 1 << op_idx <= MAX_OP_VAL ; op_idx++ ){
        if( (1 << op_idx) & op ){
            printf("%s ", op2str[ 1 << op_idx ] );
        }
    }
    printf("\n");
}

void dump_batches( struct job *job ){

    print_header();

    // longitudinals
    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        for( size_t j = 0; j < NUM_LONGITUDINAL_BATCH_TYPES; j++ ){
            for( size_t k = 0; k < job->longitudinals[i]->longitudinal_batch_count_per_type[j]; k++ ){
                for( size_t o = 0; o < job->longitudinals[i]->longitudinal_batches[j][k].numops; o++ ){
                    print_op( &( job->longitudinals[i]->longitudinal_batches[j][k].ops[o] ) );
                }
            }
        }
    }

    // polls
    for( size_t i = 0; i < job->poll_count; i++ ){
        for( size_t o = 0; o < job->polls[i]->total_ops; o++ ){
            print_op( &(job->polls[i]->poll_ops[o]) );
        }
    }
}


void run_longitudinal_batches( struct job *job, longitudinal_batch_t j ){

    static int fd=-999;

    if( SETUP == j ){
        fd = open( "/dev/cpu/msr_batch", O_RDONLY );
        assert( -1 != fd );
    }
    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        for( size_t k = 0; k < job->longitudinals[i]->longitudinal_batch_count_per_type[ j ]; k++ ){
            errno = 0;
            int rc = ioctl( fd, X86_IOC_MSR_BATCH, &(job->longitudinals[i]->longitudinal_batches[j][k] ) );
            if( 0 != rc ){
                perror("");
                exit(-1);
            }
        }
    }
    if( TEARDOWN == j ){
        close( fd );
        fd=-999;
    }
}
