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

static uint16_t max_msrsafe_cpu = UINT16_MAX;   // current limitation of msr-safe.

//////////////////////////////////////////////////////////////////////////////////
// List of MSRs
//////////////////////////////////////////////////////////////////////////////////
static constexpr const uint32_t  TIME_STAMP_COUNTER               = 0x0010;
static constexpr const uint32_t  MISC_PACKAGE_CTLS                = 0x00BC;
static constexpr const uint32_t  MPERF                            = 0x00E7;
static constexpr const uint32_t  APERF                            = 0x00E8;
static constexpr const uint32_t  ARCH_CAPABILTIES                 = 0x010A;
static constexpr const uint32_t  PERF_STATUS                      = 0x0198;
static constexpr const uint32_t  PERF_CTL                         = 0x0199;
static constexpr const uint32_t  THERM_STATUS                     = 0x019C; // 22:16 Degrees C away from max
static constexpr const uint32_t  ENERGY_PERF_BIAS                 = 0x01B0;
static constexpr const uint32_t  PACKAGE_THERM_STATUS             = 0x01B1; // 22:16 Degrees C away from max
static constexpr const uint32_t  FIXED_CTR0                       = 0x0309; // INST_RETIRED.ANY
static constexpr const uint32_t  FIXED_CTR1                       = 0x030A; // CPU_CLK_UNHALTED.[THREAD|CORE]
static constexpr const uint32_t  FIXED_CTR2                       = 0x030B; // CPU_CLK_UNHALTED.REF_TSC
static constexpr const uint32_t  FIXED_CTR3                       = 0x030C;
static constexpr const uint32_t  FIXED_CTR_CTRL                   = 0x038D;
static constexpr const uint32_t  PERF_GLOBAL_CTRL                 = 0x038F;
static constexpr const uint32_t  RAPL_POWER_UNIT                  = 0x0606;
static constexpr const uint32_t  PKG_POWER_LIMIT                  = 0x0610;
static constexpr const uint32_t  PKG_ENERGY_STATUS                = 0x0611;
static constexpr const uint32_t  PACKAGE_ENERGY_TIME_STATUS       = 0x0612;
static constexpr const uint32_t  PKG_PERF_STATUS                  = 0x0613;
static constexpr const uint32_t  PKG_POWER_INFO                   = 0x0614;
static constexpr const uint32_t  DRAM_PWER_LIMIT                  = 0x0618;
static constexpr const uint32_t  DRAM_ENERGY_STATUS               = 0x0619;
static constexpr const uint32_t  DRAM_PERF_STATUS                 = 0x061B;
static constexpr const uint32_t  DRAM_POWER_INFO                  = 0x061C;
static constexpr const uint32_t  PP0_POWER_LIMIT                  = 0x0638;
static constexpr const uint32_t  PP0_ENERGY_STATUS                = 0x0639;
static constexpr const uint32_t  PP0_POLICY                       = 0x063A;
static constexpr const uint32_t  PP1_POWER_LIMIT                  = 0x0640;
static constexpr const uint32_t  PP1_ENERGY_STATUS                = 0x0641;
static constexpr const uint32_t  PP1_POLICY                       = 0x0642;
static constexpr const uint32_t  PLATFORM_ENERGY_COUNTER          = 0x064D;
static constexpr const uint32_t  PPERF                            = 0x064E;
static constexpr const uint32_t  PLATFORM_POWER_INFO              = 0x0665;
static constexpr const uint32_t  PLATFORM_POWER_LIMIT             = 0x065C;
static constexpr const uint32_t  PLATFORM_RAPL_SOCKET_PERF_STATUS = 0x0666;
static constexpr const uint32_t  PM_ENABLE                        = 0x0770;
static constexpr const uint32_t  HWP_CAPABILITIES                 = 0x0771;

// This wastes an extravagant amount of memory if the compiler isn't optimizing
// based on the double const.  Use constexpr?
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


//////////////////////////////////////////////////////////////////////////////////
// Allowlist.
//////////////////////////////////////////////////////////////////////////////////
static char const *const allowlist =
                                    // Polling MSRs (TBD)

                                    // Everything else
                                     "0x0010 0x0000000000000000\n"      // TIME_STAMP_COUNTER
                                     "0x00BC 0x0000000000000000\n"      // MISC_PACKAGE_CTLS
                                     "0x00E7 0x0000000000000000\n"      // MPERF
                                     "0x00E8 0x0000000000000000\n"      // APERF
                                     "0x010A 0x0000000000000000\n"      // ARCH_CAPABILTIES
                                     "0x0198 0x0000000000000000\n"      // PERF_STATUS
                                     "0x0199 0x0000000000000000\n"      // PERF_CTL
                                     "0x019C 0x0000000000000000\n"      // THERM_STATUS
                                     "0x01B0 0x0000000000000000\n"      // ENERGY_PERF_BIAS
                                     "0x01B1 0x0000000000000000\n"      // PACKAGE_THERM_STATUS
                                     "0x0309 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR0
                                     "0x030A 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR1
                                     "0x030B 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR2
                                     "0x030C 0xFFFFFFFFFFFFFFFF\n"      // FIXED_CTR3
                                     "0x038D 0x0000000000000333\n"      // FIXED_CTR_CTRL
                                     "0x038F 0x0000000700000000\n"      // PERF_GLOBAL_CTRL
                                     "0x0606 0x0000000000000000\n"      // RAPL_POWER_UNIT
                                     "0x0610 0x0000000000000000\n"      // PKG_POWER_LIMIT
                                     "0x0611 0x0000000000000000\n"      // PKG_ENERGY_STATUS
                                     "0x0612 0x0000000000000000\n"      // PACKAGE_ENERGY_TIME_STATUS
                                     "0x0613 0x0000000000000000\n"      // PKG_PERF_STATUS
                                     "0x0614 0x0000000000000000\n"      // PKG_POWER_INFO
                                     "0x0618 0x0000000000000000\n"      // DRAM_PWER_LIMIT
                                     "0x0619 0x0000000000000000\n"      // DRAM_ENERGY_STATUS
                                     "0x061B 0x0000000000000000\n"      // DRAM_PERF_STATUS
                                     "0x061C 0x0000000000000000\n"      // DRAM_POWER_INFO
                                     "0x0638 0x0000000000000000\n"      // PP0_POWER_LIMIT
                                     "0x0639 0x0000000000000000\n"      // PP0_ENERGY_STATUS
                                     "0x063A 0x0000000000000000\n"      // PP0_POLICY
                                     "0x0640 0x0000000000000000\n"      // PP1_POWER_LIMIT
                                     "0x0641 0x0000000000000000\n"      // PP1_ENERGY_STATUS
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

// Polling instructions.  C and F need some attention.
static constexpr const struct msr_batch_op op_poll_pkg_J = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL | OP_THERM_INITIAL | OP_THERM_FINAL, .msr = PKG_ENERGY_STATUS, .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_pp0_J = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL | OP_THERM_INITIAL | OP_THERM_FINAL, .msr = PP0_ENERGY_STATUS, .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_pp1_J = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL | OP_THERM_INITIAL | OP_THERM_FINAL, .msr = PP1_ENERGY_STATUS, .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_dram_J ={ .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL | OP_THERM_INITIAL | OP_THERM_FINAL, .msr = DRAM_ENERGY_STATUS, .poll_max=MAX_POLL_ATTEMPTS };

static constexpr const struct msr_batch_op op_poll_pkg_C = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL, .msr = PACKAGE_THERM_STATUS,      .poll_max=MAX_POLL_ATTEMPTS };
static constexpr const struct msr_batch_op op_poll_core_C = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL, .msr = THERM_STATUS,      .poll_max=MAX_POLL_ATTEMPTS };


static constexpr const struct msr_batch_op op_poll_pkg_F = { .op = OP_POLL | OP_TSC_INITIAL | OP_TSC_FINAL | OP_TSC_POLL, .msr = PERF_STATUS,       .poll_max=MAX_POLL_ATTEMPTS };

// Single-read energy instructions.
static constexpr const struct msr_batch_op op_rd_RAPL_POWER_UNIT         = { .op = OP_READ | OP_TSC_INITIAL, .msr = RAPL_POWER_UNIT };
static constexpr const struct msr_batch_op op_rd_PKG_ENERGY_STATUS       = { .op = OP_READ | OP_TSC_INITIAL, .msr = PKG_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_DRAM_ENERGY_STATUS      = { .op = OP_READ | OP_TSC_INITIAL, .msr = DRAM_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_PP0_ENERGY_STATUS       = { .op = OP_READ | OP_TSC_INITIAL, .msr = PP0_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_PP1_ENERGY_STATUS       = { .op = OP_READ | OP_TSC_INITIAL, .msr = PP1_ENERGY_STATUS };
static constexpr const struct msr_batch_op op_rd_PLATFORM_ENERGY_COUNTER = { .op = OP_READ | OP_TSC_INITIAL, .msr = PLATFORM_ENERGY_COUNTER };

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
            job->polls[i]->poll_ops[j].err = (uint32_t)(0xDECAFBAD);
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
                    lng->batches[ slot_idx ]->ops[ (op_idx * ncpu) + cpu_idx ].err = 0xDECAFBAD;
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

#if 0
    static uint64_t previous_pkg_energy_status_value;
    static uint64_t accumulated_pkg_energy_status_rollover;
    const uint64_t ENERGY_ROLLOVER_OFFSET = 0x100000000;
    This is supposed to happen in the thread, not here
    // Only handle rollover for polls for now.
    if( o->op & OP_POLL ){
        if( o->msr == PKG_ENERGY_STATUS ){
            o->msrdata += accumulated_pkg_energy_status_rollover;
            o->msrdata2 += accumulated_pkg_energy_status_rollover;
            // If the msrdata pkg_energy_status value rolled over, assume the msrdata2 value cannot
            // within the same sample.
            if( o->msrdata < previous_pkg_energy_status_value ){
                o->msrdata  += ENERGY_ROLLOVER_OFFSET;
                o->msrdata2 += ENERGY_ROLLOVER_OFFSET;
                accumulated_pkg_energy_status_rollover += ENERGY_ROLLOVER_OFFSET;
            }
            // Rolloever happened during polling.
            else if( o->msrdata2 < o->msrdata ){
                o->msrdata2 += ENERGY_ROLLOVER_OFFSET;
                accumulated_pkg_energy_status_rollover += ENERGY_ROLLOVER_OFFSET;
            }
            previous_pkg_energy_status_value = o->msrdata2;
        }
    }
#endif
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
    printf( "# Dumping longitudinal batches\n");
    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        for( longitudinal_slot_t slot_idx = 0; slot_idx < NUM_LONGITUDINAL_EXECUTION_SLOTS; slot_idx++ ){
            if( NULL == job->longitudinals[i]->batches[ slot_idx ] ){
                continue;
            }
            for( size_t op_idx = 0; op_idx < job->longitudinals[i]->batches[slot_idx]->numops; op_idx++ ){
                print_op( &( job->longitudinals[i]->batches[slot_idx]->ops[op_idx] ) );
            }
        }
    }

    // polls
    printf( "# Dumping poll batches\n");
    for( size_t i = 0; i < job->poll_count; i++ ){
        for( size_t o = 0; o < job->polls[i]->total_ops; o++ ){
            print_op( &(job->polls[i]->poll_ops[o]) );
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
