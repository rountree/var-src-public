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
#define MSR_SAFE_USERSPACE  // Pick up some typedefs and enums useful for printing ops
#include "msr_safe.h"       // struct msr_batch_array, struct msr_batch_op, X86_IOC_MSR_BATCH
#include "msr_version.h"    // MSR_SAFE_VERSION_u32
#include "cpuset_utils.h"   // get_next_cpu()
#include "msr_utils.h"

#define EXTRACT_TEMPERATURE(x) ( (x>>16) & 0x7fULL )
#define UNUSED_OP ((__s32)(0xDECAFBAD))

static uint16_t max_msrsafe_cpu = UINT16_MAX;   // current limitation of msr-safe.

//////////////////////////////////////////////////////////////////////////////////
// List of MSRs
//////////////////////////////////////////////////////////////////////////////////
typedef enum : uint64_t{
    TIME_STAMP_COUNTER               = 0x0010,
    MISC_PACKAGE_CTLS                = 0x00BC,
    MPERF                            = 0x00E7,
    APERF                            = 0x00E8,
    ARCH_CAPABILTIES                 = 0x010A,
    PERF_STATUS                      = 0x0198,
    PERF_CTL                         = 0x0199,
    THERM_STATUS                     = 0x019C, // 22:16 Degrees C away from max
    ENERGY_PERF_BIAS                 = 0x01B0,
    PACKAGE_THERM_STATUS             = 0x01B1, // 22:16 Degrees C away from max
    FIXED_CTR0                       = 0x0309, // INST_RETIRED.ANY
    FIXED_CTR1                       = 0x030A, // CPU_CLK_UNHALTED.[THREAD|CORE]
    FIXED_CTR2                       = 0x030B, // CPU_CLK_UNHALTED.REF_TSC
    FIXED_CTR3                       = 0x030C,
    FIXED_CTR_CTRL                   = 0x038D,
    PERF_GLOBAL_CTRL                 = 0x038F,
    RAPL_POWER_UNIT                  = 0x0606,
    PKG_POWER_LIMIT                  = 0x0610,
    PKG_ENERGY_STATUS                = 0x0611,
    PACKAGE_ENERGY_TIME_STATUS       = 0x0612,
    PKG_PERF_STATUS                  = 0x0613,
    PKG_POWER_INFO                   = 0x0614,
    DRAM_PWER_LIMIT                  = 0x0618,
    DRAM_ENERGY_STATUS               = 0x0619,
    DRAM_PERF_STATUS                 = 0x061B,
    DRAM_POWER_INFO                  = 0x061C,
    PP0_POWER_LIMIT                  = 0x0638,
    PP0_ENERGY_STATUS                = 0x0639,
    PP0_POLICY                       = 0x063A,
    PP1_POWER_LIMIT                  = 0x0640,
    PP1_ENERGY_STATUS                = 0x0641,
    PP1_POLICY                       = 0x0642,
    PLATFORM_ENERGY_COUNTER          = 0x064D,
    PPERF                            = 0x064E,
    PLATFORM_POWER_INFO              = 0x0665,
    PLATFORM_POWER_LIMIT             = 0x065C,
    PLATFORM_RAPL_SOCKET_PERF_STATUS = 0x0666,
    PM_ENABLE                        = 0x0770,
    HWP_CAPABILITIES                 = 0x0771,
}msr_t;

// This wastes an extravagant amount of memory if the compiler isn't optimizing
// based on the double const.  Use constexpr?
#if 0
This should be coming out of an msr header file.
static const char * const msr2str[] = {
       [TIME_STAMP_COUNTER]                = "TIME_STAMP_COUNTER",
       [MISC_PACKAGE_CTLS]                 = "MISC_PACKAGE_CTLS",
       [MPERF]                             = "MPERF",
       [APERF]                             = "APERF",
       [ARCH_CAPABILTIES]                  = "ARCH_CAPABILTIES",
       [PERF_STATUS]                       = "PERF_STATUS",
       [PERF_CTL]                          = "PERF_CTL",
       [THERM_STATUS]                      = "THERM_STATUS",
       [ENERGY_PERF_BIAS]                  = "ENERGY_PERF_BIAS",
       [PACKAGE_THERM_STATUS]              = "PACKAGE_THERM_STATUS",
       [FIXED_CTR0]                        = "FIXED_CTR0",
       [FIXED_CTR1]                        = "FIXED_CTR1",
       [FIXED_CTR2]                        = "FIXED_CTR2",
       [FIXED_CTR3]                        = "FIXED_CTR3",
       [FIXED_CTR_CTRL]                    = "FIXED_CTR_CTRL",
       [PERF_GLOBAL_CTRL]                  = "PERF_GLOBAL_CTRL",
       [RAPL_POWER_UNIT]                   = "RAPL_POWER_UNIT",
       [PKG_POWER_LIMIT]                   = "PKG_POWER_LIMIT",
       [PKG_ENERGY_STATUS]                 = "PKG_ENERGY_STATUS",
       [PACKAGE_ENERGY_TIME_STATUS]        = "PACKAGE_ENERGY_TIME_STATUS",
       [PKG_PERF_STATUS]                   = "PKG_PERF_STATUS",
       [PKG_POWER_INFO]                    = "PKG_POWER_INFO",
       [DRAM_PWER_LIMIT]                   = "DRAM_PWER_LIMIT",
       [DRAM_ENERGY_STATUS]                = "DRAM_ENERGY_STATUS",
       [DRAM_PERF_STATUS]                  = "DRAM_PERF_STATUS",
       [DRAM_POWER_INFO]                   = "DRAM_POWER_INFO",
       [PP0_POWER_LIMIT]                   = "PP0_POWER_LIMIT",
       [PP0_ENERGY_STATUS]                 = "PP0_ENERGY_STATUS",
       [PP0_POLICY]                        = "PP0_POLICY",
       [PP1_POWER_LIMIT]                   = "PP1_POWER_LIMIT",
       [PP1_ENERGY_STATUS]                 = "PP1_ENERGY_STATUS",
       [PP1_POLICY]                        = "PP1_POLICY",
       [PLATFORM_ENERGY_COUNTER]           = "PLATFORM_ENERGY_COUNTER",
       [PPERF]                             = "PPERF",
       [PLATFORM_POWER_INFO]               = "PLATFORM_POWER_INFO",
       [PLATFORM_POWER_LIMIT]              = "PLATFORM_POWER_LIMIT",
       [PLATFORM_RAPL_SOCKET_PERF_STATUS]  = "PLATFORM_RAPL_SOCKET_PERF_STATUS",
       [PM_ENABLE]                         = "PM_ENABLE",
       [HWP_CAPABILITIES]                  = "HWP_CAPABILITIES",
};
#endif

//////////////////////////////////////////////////////////////////////////////////
// Allowlist.
//////////////////////////////////////////////////////////////////////////////////
static char const *const allowlist =
    // Polling MSRs (incomplete)
    "0x0639 0x0000000000000000\n"      // PP0_ENERGY_STATUS
    "0x0611 0x0000000000000000\n"      // PKG_ENERGY_STATUS
    "0x0619 0x0000000000000000\n"      // DRAM_ENERGY_STATUS
    "0x0641 0x0000000000000000\n"      // PP1_ENERGY_STATUS

    // Everything else
    "0x0010 0x0000000000000000\n"      // TIME_STAMP_COUNTER
    "0x00BC 0x0000000000000000\n"      // MISC_PACKAGE_CTLS
    "0x00E7 0x0000000000000000\n"      // MPERF
    "0x00E8 0x0000000000000000\n"      // APERF
    "0x010A 0x0000000000000000\n"      // ARCH_CAPABILTIES
    "0x0198 0x0000000000000000\n"      // PERF_STATUS
    "0x0199 0x0000000000000000\n"      // PERF_CTL
    "0x019C 0x0000000000000000\n"      // THERM_STATUS         (bits 22:16 contain "Package digital temperature reading in 1 degree Celsius relative to the package TCC activation temperature." p16-46, etc.)
    "0x01B0 0x0000000000000000\n"      // ENERGY_PERF_BIAS
    "0x01B1 0x0000000000000000\n"      // PACKAGE_THERM_STATUS (bits 22:16 contain "Package digital temperature reading in 1 degree Celsius relative to the package TCC activation temperature." p16-46, v3B, 253669-086US, Dec 2024)
    "0x0309 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR0
    "0x030A 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR1
    "0x030B 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR2
    "0x030C 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR3
    "0x038D 0x0000000000000333\n"      // FIXED_CTR_CTRL
    "0x038F 0x0000000700000000\n"      // PERF_GLOBAL_CTRL
    "0x0606 0x0000000000000000\n"      // RAPL_POWER_UNIT
    "0x0610 0x0000000000000000\n"      // PKG_POWER_LIMIT
    "0x0612 0x0000000000000000\n"      // PACKAGE_ENERGY_TIME_STATUS
    "0x0613 0x0000000000000000\n"      // PKG_PERF_STATUS
    "0x0614 0x0000000000000000\n"      // PKG_POWER_INFO
    "0x0618 0x0000000000000000\n"      // DRAM_POWER_LIMIT
    "0x061B 0x0000000000000000\n"      // DRAM_PERF_STATUS
    "0x061C 0x0000000000000000\n"      // DRAM_POWER_INFO
    "0x0638 0x0000000000000000\n"      // PP0_POWER_LIMIT
    "0x063A 0x0000000000000000\n"      // PP0_POLICY
    "0x0640 0x0000000000000000\n"      // PP1_POWER_LIMIT
    "0x0642 0x0000000000000000\n"      // PP1_POLICY
    "0x064D 0x0000000000000000\n"      // PLATFORM_ENERGY_COUNTER
    "0x064E 0x0000000000000000\n"      // PPERF
    "0x0665 0x0000000000000000\n"      // PLATFORM_POWER_INFO
    "0x065C 0x0000000000000000\n"      // PLATFORM_POWER_LIMIT
    "0x0666 0x0000000000000000\n"      // PLATFORM_RAPL_SOCKET_PERF_STATUS
    "0x0770 0x0000000000000000\n"      // PM_ENABLE
    "0x0771 0x0000000000000000\n"      // HWP_CAPABILITIES
    ;

static constexpr const uint32_t MAX_POLL_ATTEMPTS = 10000;

//////////////////////////////////////////////////////////////////////////////////
// Available ops.
//////////////////////////////////////////////////////////////////////////////////
static constexpr const struct msr_batch_op op_zero_fixed_ctr0 = { .op = OP_WRITE | OP_TSC, .msr = FIXED_CTR0 };
static constexpr const struct msr_batch_op op_zero_fixed_ctr1 = { .op = OP_WRITE | OP_TSC, .msr = FIXED_CTR1 };
static constexpr const struct msr_batch_op op_zero_fixed_ctr2 = { .op = OP_WRITE | OP_TSC, .msr = FIXED_CTR2 };

static constexpr const struct msr_batch_op op_read_fixed_ctr0 = { .op = OP_READ | OP_TSC, .msr = FIXED_CTR0 };
static constexpr const struct msr_batch_op op_read_fixed_ctr1 = { .op = OP_READ | OP_TSC, .msr = FIXED_CTR1 };
static constexpr const struct msr_batch_op op_read_fixed_ctr2 = { .op = OP_READ | OP_TSC, .msr = FIXED_CTR2 };

// Enable all three fixed-function performance counters, non-global
static constexpr const struct msr_batch_op op_enable_fixed = { .op = OP_WRITE | OP_TSC, .msr = FIXED_CTR_CTRL, .msrdata=0x333 };

// Enable/disable all fixed and programmable counters, global.  (Just fixed counters for now.)
static constexpr const struct msr_batch_op op_start_global = { .op = OP_WRITE | OP_TSC, .msr = PERF_GLOBAL_CTRL, .msrdata=0x700000000 };
static constexpr const struct msr_batch_op op_stop_global  = { .op = OP_WRITE | OP_TSC, .msr = PERF_GLOBAL_CTRL, .msrdata=0x000000000 };

// Polling instructions.  C and F need some attention.
static constexpr const struct msr_batch_op op_poll_pkg_J = { .op = OP_POLL | OP_ALL_MODS, .msr = PKG_ENERGY_STATUS,    .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_pp0_J = { .op = OP_POLL | OP_ALL_MODS, .msr = PP0_ENERGY_STATUS,    .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_pp1_J = { .op = OP_POLL | OP_ALL_MODS, .msr = PP1_ENERGY_STATUS,    .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_dram_J= { .op = OP_POLL | OP_ALL_MODS, .msr = DRAM_ENERGY_STATUS,   .poll_max=MAX_POLL_ATTEMPTS };

static constexpr const struct msr_batch_op op_poll_pkg_C = { .op = OP_POLL | OP_ALL_MODS, .msr = PACKAGE_THERM_STATUS, .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_core_C= { .op = OP_POLL | OP_ALL_MODS, .msr = THERM_STATUS,         .poll_max=MAX_POLL_ATTEMPTS };


static constexpr const struct msr_batch_op op_poll_pkg_F = { .op = OP_POLL | OP_ALL_MODS, .msr = PERF_STATUS,          .poll_max=MAX_POLL_ATTEMPTS };

// Single-read energy instructions.
static constexpr const struct msr_batch_op op_rd_RAPL_POWER_UNIT         = { .op = OP_READ | OP_ALL_MODS, .msr = RAPL_POWER_UNIT };
static constexpr const struct msr_batch_op op_rd_PKG_ENERGY_STATUS       = { .op = OP_READ | OP_ALL_MODS, .msr = PKG_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_DRAM_ENERGY_STATUS      = { .op = OP_READ | OP_ALL_MODS, .msr = DRAM_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_PP0_ENERGY_STATUS       = { .op = OP_READ | OP_ALL_MODS, .msr = PP0_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_PP1_ENERGY_STATUS       = { .op = OP_READ | OP_ALL_MODS, .msr = PP1_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_PLATFORM_ENERGY_COUNTER = { .op = OP_READ | OP_ALL_MODS, .msr = PLATFORM_ENERGY_COUNTER };

//////////////////////////////////////////////////////////////////////////////////
// Working around C initialization limitations
//
// These must be NULL-terminated.
//
//////////////////////////////////////////////////////////////////////////////////

// FIXED_FUNCTION_COUNTERS
static const struct msr_batch_op * const fixed_function_counters__setup[] = {
    &op_stop_global, &op_zero_fixed_ctr0, &op_zero_fixed_ctr1, &op_zero_fixed_ctr2, &op_enable_fixed, NULL };
static const struct msr_batch_op * const fixed_function_counters__start[] = {
    &op_start_global, NULL };
static const struct msr_batch_op * const fixed_function_counters__stop[] = {
    &op_stop_global, NULL };
static const struct msr_batch_op * const fixed_function_counters__read[] = {
    &op_read_fixed_ctr0, &op_read_fixed_ctr1, &op_read_fixed_ctr2, NULL };
static const struct msr_batch_op * const fixed_function_counters__teardown[] = {
    NULL };

// ENERGY_COUNTERS
static const struct msr_batch_op * const energy_counters__setup[] = { &op_rd_RAPL_POWER_UNIT, NULL };
static const struct msr_batch_op * const energy_counters__start[] = {
    &op_rd_PKG_ENERGY_STATUS, &op_rd_DRAM_ENERGY_STATUS, &op_rd_PP0_ENERGY_STATUS, &op_rd_PP1_ENERGY_STATUS, &op_rd_PLATFORM_ENERGY_COUNTER, NULL };
static const struct msr_batch_op * const energy_counters__stop[] = {
    &op_rd_PKG_ENERGY_STATUS, &op_rd_DRAM_ENERGY_STATUS, &op_rd_PP0_ENERGY_STATUS, &op_rd_PP1_ENERGY_STATUS, &op_rd_PLATFORM_ENERGY_COUNTER, NULL };
static const struct msr_batch_op * const energy_counters__read[] = { NULL };
static const struct msr_batch_op * const energy_counters__teardown[] = { NULL };

static const struct msr_batch_op * const * const fixed_function_counters__ops[ NUM_LONGITUDINAL_EXECUTION_SLOTS ] = {
    fixed_function_counters__setup,
    fixed_function_counters__start,
    fixed_function_counters__stop,
    fixed_function_counters__read,
    fixed_function_counters__teardown };

static const struct msr_batch_op * const * const energy_counters__ops[ NUM_LONGITUDINAL_EXECUTION_SLOTS ] = {
    energy_counters__setup,
    energy_counters__start,
    energy_counters__stop,
    energy_counters__read,
    energy_counters__teardown };

static const struct msr_batch_op * const * const * const longitudinal_recipes[ NUM_LONGITUDINAL_FUNCTIONS ] = {
    fixed_function_counters__ops, energy_counters__ops };


//////////////////////////////////////////////////////////////////////////////////
// Now on to something that isn't datatype hell.
//////////////////////////////////////////////////////////////////////////////////


void teardown_msrsafe_batches( struct job *job ){
    // Polls are easy.
    for( size_t i = 0; i < job->poll_count; i++ ){
        free( job->polls[i]->poll_batches );
        free( job->polls[i]->poll_ops );
    }
    // Longitudinals are a little tricker.
    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        for( longitudinal_slot_t slot_idx = 0; slot_idx < NUM_LONGITUDINAL_EXECUTION_SLOTS; slot_idx++ ){
            if( NULL == job->longitudinals[i]->batches[slot_idx] ){
                continue;
            }
            free( job->longitudinals[i]->batches[slot_idx]->ops );
            free( job->longitudinals[i]->batches[slot_idx] );
        }
    }
}

static void setup_polling_batches( struct job *job ){

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
            case PKG_ENERGY:
                op = &op_poll_pkg_J;
                break;
            case PP0_ENERGY:
                op = &op_poll_pp0_J;
                break;
            case PP1_ENERGY:
                op = &op_poll_pp1_J;
                break;
            case DRAM_ENERGY:
                op = &op_poll_dram_J;
                break;
            case CORE_THERMAL:
                op = &op_poll_core_C;
                break;
            case PKG_THERMAL:
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
            job->polls[i]->poll_ops[j].err = UNUSED_OP;
       }
    }


}

static void setup_longitudinal_batches( struct job *job ){

    // Map the longitudinal functions we're using onto msr-safe batches.
    static bool initialized;
    static size_t ops_per_function_per_slot[ NUM_LONGITUDINAL_FUNCTIONS ][ NUM_LONGITUDINAL_EXECUTION_SLOTS ];

    if( !initialized ){
        // This is stupid, but it's better than hard-coding ops per function:slot tuple.
        // But not that much better.  (The compiler has enough information to figure this out.
        // It just doesn't yet.)
        for( longitudinal_t lng_idx = 0; lng_idx < NUM_LONGITUDINAL_FUNCTIONS; lng_idx++ ){
            for( longitudinal_slot_t slot_idx = 0; slot_idx < NUM_LONGITUDINAL_EXECUTION_SLOTS; slot_idx++ ){
                const struct msr_batch_op * const *p = longitudinal_recipes[ lng_idx ][ slot_idx ];
                while( *p++ ){
                    ops_per_function_per_slot[ lng_idx ][ slot_idx ]++;
                }
            }
        }
        initialized = true;
    }

    for( size_t i = 0; i < job->longitudinal_count; i++ ){

        uint32_t ncpu = CPU_COUNT( &(job->longitudinals[i]->sample_cpus) );

        // Local variables for terseness.
        struct longitudinal_config *lng = job->longitudinals[i];

        for( longitudinal_slot_t slot_idx = 0; slot_idx < NUM_LONGITUDINAL_EXECUTION_SLOTS; slot_idx++ ){

            // How many operations are in each slot of this longitudinal function?
            uint32_t ops_per_cpu = ops_per_function_per_slot[ lng->longitudinal_type ][ slot_idx ];

            if( 0 == ops_per_cpu ){
                lng->batches[ slot_idx ] = NULL;
                continue;
            }
            uint32_t total_ops = ncpu * ops_per_cpu;

            // Allocate the msr_batch_array struct and populate it.
            lng->batches[ slot_idx ]            = calloc( 1, sizeof( struct msr_batch_array ) );
            lng->batches[ slot_idx ]->numops    = total_ops;
            lng->batches[ slot_idx ]->version   = MSR_SAFE_VERSION_u32;
            lng->batches[ slot_idx ]->ops       = calloc( total_ops, sizeof( struct msr_batch_op ) );
            // Make copies of the operations listed in longitudinal_recipes[ functions ][ slots ][ ops ]
            for( uint32_t op_idx = 0; op_idx < ops_per_cpu; op_idx++ ){
                for ( uint32_t cpu_idx = 0, current_cpu = 0; cpu_idx < ncpu; cpu_idx++ ){
                    memcpy( &(lng->batches[ slot_idx ]->ops[ (op_idx * ncpu) + cpu_idx ]),
                            longitudinal_recipes[ lng->longitudinal_type ][ slot_idx ][ op_idx ],
                            sizeof( struct msr_batch_op ) );
                    lng->batches[ slot_idx ]->ops[ (op_idx * ncpu) + cpu_idx ].err = UNUSED_OP;
                    current_cpu = get_next_cpu( current_cpu, max_msrsafe_cpu, &(lng->sample_cpus) );
                    lng->batches[ slot_idx ]->ops[ (op_idx * ncpu) + cpu_idx ].cpu = current_cpu;
                    current_cpu++;
                }
            }
        }
    }
}

void setup_msrsafe_batches( struct job *job ){

    setup_polling_batches( job );
    setup_longitudinal_batches( job );
}

void populate_allowlist( void ) {

    // Keep it manual.
    int fd = open("/dev/cpu/msr_allowlist", O_WRONLY);
    assert(-1 != fd);
    ssize_t nbytes = write(fd, allowlist, strlen(allowlist));
    if( -1 == nbytes ){
        fprintf( stderr, "%s:%d:%s Loading allowlist failed:  (%d) %s.\n",
                __FILE__, __LINE__, __func__, errno, strerror( errno ) );
        exit(-1);
    }
    if( nbytes != strlen(allowlist) ){
        fprintf( stderr, "%zd bytes written to /dev/cpu/msr_allowlist, expected to write %zu.\n", nbytes, strlen(allowlist) );
        exit(-1);
    }
    close(fd);
}


static void print_header( int fd, uint64_t op_bitfield ){
    static bool is_first = true;
    if( 0 == op_bitfield ){
        dprintf( fd, "# No column headers requested\n");
    }else{
        dprintf( fd, "# bitfield for header selection is:  %#"PRIx64"\n", op_bitfield );
        for( op_field_arridx_t arridx = 0; arridx < op_field_arridx_MAX_IDX; arridx++ ){
            if( op_bitfield & ( 1 << arridx ) ){
                dprintf( fd, is_first ? "%s" : " %s", opfield2str[ arridx ] );
                is_first = false;
            }
        }
        dprintf( fd, "\n" );
    }
    return;
}

static void print_op( int fd, uint64_t op_bitfield, struct msr_batch_op *o, struct msr_batch_op *prev, bool skip_unused ){

    if( ( o->err == UNUSED_OP ) && skip_unused ){
        return;
    }

    static bool is_first = true;    // Add a leading space to all but the first field

    if( 0 == op_bitfield ){
        dprintf( fd, "# No column headers requested\n");
    }else{
        for( op_field_arridx_t arridx = 0; arridx < op_field_arridx_MAX_IDX; arridx++ ){
            if( op_bitfield & ( 1 << arridx ) ){
                // extra leading space for all but the first value
                if( is_first ){
                    is_first = false;
                }else{
                    dprintf( fd, " " );
                }
                // print out the formatted value
                // casting required because the kernel's _u64 is not quite the same time as uint64_t
                // (unsigned long vs unsigned long long, I think, though I forget which was which)
                switch( arridx ){
                    case op_field_arridx_CPU:           dprintf( fd, "%#"PRIx16, (uint16_t)(o->cpu) );          break;
                    case op_field_arridx_OP:            dprintf( fd, "%#"PRIx16, (uint16_t)(o->op) );           break;
                    case op_field_arridx_ERR:           dprintf( fd, "%#"PRIx32, ( int32_t)(o->err) );          break;
                    case op_field_arridx_POLL_MAX:      dprintf( fd, "%#"PRIx32, (uint32_t)(o->poll_max) );     break;
                    case op_field_arridx_WMASK:         dprintf( fd, "%#"PRIx64, (uint64_t)(o->wmask) );        break;
                    case op_field_arridx_MSR:           dprintf( fd, "%#"PRIx32, (uint32_t)(o->msr) );          break;
                    case op_field_arridx_MSRDATA:       dprintf( fd, "%#"PRIx64, (uint64_t)(o->msrdata) );      break;
                    case op_field_arridx_MSRDATA2:      dprintf( fd, "%#"PRIx64, (uint64_t)(o->msrdata2) );     break;
                    case op_field_arridx_TSC:           dprintf( fd, "%#"PRIx64, (uint64_t)(o->tsc) );          break;
                    case op_field_arridx_MPERF:         dprintf( fd, "%#"PRIx64, (uint64_t)(o->mperf) );        break;
                    case op_field_arridx_APERF:         dprintf( fd, "%#"PRIx64, (uint64_t)(o->aperf) );        break;
                    case op_field_arridx_THERM:         dprintf( fd, "%#"PRIx64, (uint64_t)(o->therm) );        break;
                    case op_field_arridx_PTHERM:        dprintf( fd, "%#"PRIx64, (uint64_t)(o->ptherm) );       break;
                    case op_field_arridx_TAG:           dprintf( fd, "%#"PRIx64, (uint64_t)(o->tag) );          break;
                    case op_field_arridx_DELTA_MPERF:   if( prev && ( prev->err != UNUSED_OP ) ){ dprintf( fd, "%"PRId64, (int64_t)( o->mperf   - prev->mperf   ) ); } break;
                    case op_field_arridx_DELTA_APERF:   if( prev && ( prev->err != UNUSED_OP ) ){ dprintf( fd, "%"PRId64, (int64_t)( o->aperf   - prev->aperf   ) ); } break;
                    case op_field_arridx_DELTA_TSC:     if( prev && ( prev->err != UNUSED_OP ) ){ dprintf( fd, "%"PRId64, (int64_t)( o->tsc     - prev->tsc     ) ); } break;
                    case op_field_arridx_DELTA_THERM:   if( prev && ( prev->err != UNUSED_OP ) ){ dprintf( fd, "%"PRId64, (int64_t)( o->therm   - prev->therm   ) ); } break;
                    case op_field_arridx_DELTA_PTHERM:  if( prev && ( prev->err != UNUSED_OP ) ){ dprintf( fd, "%"PRId64, (int64_t)( o->ptherm  - prev->ptherm  ) ); } break;
                    case op_field_arridx_DELTA_MSRDATA: if( prev && ( prev->err != UNUSED_OP ) ){ dprintf( fd, "%"PRId64, (int64_t)( o->msrdata - prev->msrdata ) ); } break;
                    default:
                        fprintf( stderr, "%s:%d:%s Unknown value for arridx:  %#"PRIx64"\n", __FILE__, __LINE__, __func__, arridx );
                        assert(0);
                        break;
                }
            }
        }
        dprintf( fd, "\n" );
    }
    return;

#if 0
    printf("%s ", msr2str[ o->msr ]);
    uint16_t op = o->op;
    for( size_t op_idx = 0; 1 << op_idx <= MAX_OP_VAL ; op_idx++ ){
        if( (1 << op_idx) & op ){
            printf("%s ", op2str[ 1 << op_idx ] );
        }
    }
#endif
    printf("\n");
}

static void print_summaries( struct job *job ){

    for( size_t i = 0; i < job->poll_count; i++ ){
        if( job->polls[i]->total_ops < 2 ){
            // If we don't have at least two ops, give up.
            continue;
        }
        if( UNUSED_OP == job->polls[i]->poll_ops[0].err
         || UNUSED_OP == job->polls[i]->poll_ops[0].err){
            // If either or both of the first two ops were never used, give up.
            continue;
        }
        // Find the last entry
        size_t o;
        for( o = 0; o < job->polls[i]->total_ops; o++ ){
            if( UNUSED_OP == job->polls[i]->poll_ops[o].err ){
                break;
            }
        }
        o--;    // We want the last-non-DECAFBAD op.
        printf( "# SUMMARY %s\n", polltype2str[ job->polls[i]->poll_type ] );
        if( PKG_ENERGY == job->polls[i]->poll_type
         || PP0_ENERGY == job->polls[i]->poll_type
         || PP1_ENERGY == job->polls[i]->poll_type
         || DRAM_ENERGY == job->polls[i]->poll_type){
            printf( "# SUMMARY delta msrdata = %llu\n",
                    job->polls[i]->poll_ops[o].msrdata - job->polls[i]->poll_ops[0].msrdata );
        }
        printf( "# SUMMARY delta tsc     = %llu\n",
                job->polls[i]->poll_ops[o].tsc - job->polls[i]->poll_ops[0].tsc);
        printf( "# SUMMARY initial C     = %llu\n",
                EXTRACT_TEMPERATURE( job->polls[i]->poll_ops[0].therm) );
        printf( "# SUMMARY final C       = %llu\n",
                EXTRACT_TEMPERATURE( job->polls[i]->poll_ops[o].therm) );

    }
}

static void cleanup_poll_data( struct job *job ){

    uint64_t cumulative_adjustment = 0;
    constexpr const uint64_t rollover_adjustment = 1ULL << 32;

    for( size_t i = 0; i < job->poll_count; i++ ){
        if( job->polls[i]->poll_type == PKG_ENERGY
         || job->polls[i]->poll_type == PP0_ENERGY
         || job->polls[i]->poll_type == PP1_ENERGY
         || job->polls[i]->poll_type == DRAM_ENERGY
        ){
            for( size_t b = 0; b < job->polls[i]->total_ops; b++ ){

                // Handle the rollover case here so we don't have to reinvent solutions
                // in the analysis phase.
                job->polls[i]->poll_ops[b].msrdata  += cumulative_adjustment;
                job->polls[i]->poll_ops[b].msrdata2 += cumulative_adjustment;

                // 1. Check to see if rollover happened within a poll op.
                if( job->polls[i]->poll_ops[b].msrdata2 < job->polls[i]->poll_ops[b].msrdata ){
                    job->polls[i]->poll_ops[b].msrdata2 += rollover_adjustment;
                    cumulative_adjustment += rollover_adjustment;

                // 2. Check to see if rollover happened between ops.
                }else if( (b > 0) && (job->polls[i]->poll_ops[b].msrdata < job->polls[i]->poll_ops[b-1].msrdata2) ){
                    job->polls[i]->poll_ops[b].msrdata  += rollover_adjustment;
                    job->polls[i]->poll_ops[b].msrdata2 += rollover_adjustment;
                    cumulative_adjustment += rollover_adjustment;
                }
            }
        }
    }

    for( size_t i = 0; i < job->poll_count; i++ ){
        for( size_t b = 0; b < job->polls[i]->total_ops; b++ ){
            if( job->polls[i]->poll_ops[b].op & OP_THERM ){
                job->polls[i]->poll_ops[b].therm = (job->polls[i]->poll_ops[b].therm >> 16) & 0x7f;
            }
            if( job->polls[i]->poll_ops[b].op & OP_PTHERM ){
                job->polls[i]->poll_ops[b].ptherm = (job->polls[i]->poll_ops[b].ptherm >> 16) & 0x7f;
            }
            job->polls[i]->poll_ops[b].wmask =  job->polls[i]->poll_ops[b].op >> 12;
        }
    }
}

static void print_execution_counts( struct job *job ){
    printf("# benchmark_id thread_id executionA executionB\n");
    for( size_t i = 0; i < job->benchmark_count; i++ ){
       for( size_t thread_idx = 0; thread_idx < job->benchmarks[ i ]->thread_count; thread_idx++ ){
          printf( "# %02zu %02zu %15"PRIu64" %15"PRIu64"\n", i, thread_idx, job->benchmarks[ i ]->executed_loops[0][ thread_idx ], job->benchmarks[ i ]->executed_loops[1][ thread_idx ] );
       }
    }
}


void dump_batches( struct job *job ){

    if( job->benchmark_count ){
        print_execution_counts( job );
    }

    if( job->longitudinal_count ){

        // longitudinals
        printf( "# Dumping longitudinal batches\n");
        for( size_t i = 0; i < job->longitudinal_count; i++ ){
            for( longitudinal_slot_t slot_idx = 0; slot_idx < NUM_LONGITUDINAL_EXECUTION_SLOTS; slot_idx++ ){
                if( NULL == job->longitudinals[i]->batches[ slot_idx ] ){
                    continue;
                }
                static char filename[2048];
                snprintf( filename, 2047, "./longitudinal_%zu_%s_%s.out",
                        i,
                        longitudinaltype2str[ job->longitudinals[i]->longitudinal_type ],
                        longitudinalslot2str[ slot_idx ] );
                int fd = open( filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
                if( fd < 0 ){
                    perror("");
                    fprintf( stderr, "%s:%d:%s Error opening file %s.  Bye!\n", __FILE__, __LINE__, __func__, filename );
                    exit(-1);
                }
                fprintf( stderr, "%s:%d:%s op_field_bitidx_CPU    = %#"PRIx64"\n",     __FILE__, __LINE__, __func__, op_field_bitidx_CPU );
                fprintf( stderr, "%s:%d:%s op_field_bitidx_ERR    = %#"PRIx64"\n",      __FILE__, __LINE__, __func__, op_field_bitidx_ERR );
                fprintf( stderr, "%s:%d:%s op_field_bitidx_MSR    = %#"PRIx64"\n",      __FILE__, __LINE__, __func__, op_field_bitidx_MSR );
                fprintf( stderr, "%s:%d:%s op_field_bitidx_MSRDATA= %#"PRIx64"\n",      __FILE__, __LINE__, __func__, op_field_bitidx_MSRDATA );
                fprintf( stderr, "%s:%d:%s op_field_bitidx_TSC    = %#"PRIx64"\n",      __FILE__, __LINE__, __func__, op_field_bitidx_TSC );
                fprintf( stderr, "%s:%d:%s                       |= %#"PRIx64"\n",      __FILE__, __LINE__, __func__, 
                        op_field_bitidx_CPU | op_field_bitidx_ERR | op_field_bitidx_MSR | op_field_bitidx_MSRDATA | op_field_bitidx_TSC);

                print_header( fd,op_field_bitidx_CPU | op_field_bitidx_ERR | op_field_bitidx_MSR | op_field_bitidx_MSRDATA | op_field_bitidx_TSC);
                for( size_t op_idx = 0; op_idx < job->longitudinals[i]->batches[slot_idx]->numops; op_idx++ ){
                    print_op( fd, op_field_bitidx_CPU | op_field_bitidx_ERR | op_field_bitidx_MSR | op_field_bitidx_MSRDATA | op_field_bitidx_TSC,
                            &( job->longitudinals[i]->batches[slot_idx]->ops[op_idx] ), NULL, false);
                }
                close( fd );
            }
        }
    }

    if( job->poll_count ){

        // polls
        cleanup_poll_data( job );
        print_summaries( job );
        printf( "# Dumping poll batches\n");
        for( size_t i = 0; i < job->poll_count; i++ ){

            // Open up a bunch of files.
            static char filename[2048];
            int fd[ op_field_arridx_MAX_IDX ];
            for( op_field_arridx_t arridx = 0; arridx < op_field_arridx_MAX_IDX; arridx++ ){
                snprintf( filename, 2047, "./poll_%zu_%s_%s.out", i, polltype2str[ job->polls[i]->poll_type ], opfield2str[ arridx ]);
                fd[ arridx ] = open( filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
                if( fd[ arridx ] < 0 ){
                    perror("");
                    fprintf( stderr, "%s:%d:%s Error opening file %s.  Bye!\n", __FILE__, __LINE__, __func__, filename );
                    exit(-1);
                }
                print_header( fd[ arridx ], 1ULL << arridx );
            }
            // Probably faster to visit each operation once.
            // Note that we start with the second poll value to make the deltas work.
            assert( job->polls[i]->total_ops > 2 );
            for( size_t o = 1; o < job->polls[i]->total_ops; o++ ){
                for( op_field_arridx_t arridx = 0; arridx < op_field_arridx_MAX_IDX; arridx++ ){
                    print_op( fd[ arridx ], 1ULL << arridx, &(job->polls[i]->poll_ops[o]), &(job->polls[i]->poll_ops[ o-1 ]), false );
                }
            }
            // Close a bunch of files.
            for( op_field_arridx_t arridx = 0; arridx < op_field_arridx_MAX_IDX; arridx++ ){
                close( fd[ arridx ] );
            }
        }
    }
}


void run_longitudinal_batches( struct job *job, longitudinal_slot_t slot_idx ){

    static int initialized, fd;
    if( !initialized && slot_idx != TEARDOWN ){
        initialized = 1;
        fd = open( "/dev/cpu/msr_batch", O_RDONLY );
        assert( -1 != fd );
    }

    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        if( NULL == job->longitudinals[i]->batches[slot_idx] ){
            continue;
        }
        errno = 0;
        int rc = ioctl( fd, X86_IOC_MSR_BATCH, job->longitudinals[i]->batches[slot_idx] );
        if( 0 != rc ){
            dump_batches( job );
            fprintf( stderr, "%s:%d:%s ioctl failed with code (%d):  %s.\n"
                             "         longitudinal batch type = %d.\n"
                             "         slot = %d.\n",
                    __FILE__, __LINE__, __func__, errno, strerror( errno ),
                    job->longitudinals[i]->longitudinal_type,
                    slot_idx);
            exit(-1);
        }
    }

    if( slot_idx == TEARDOWN ){
        close( fd );
        initialized = 0;
    }
}
