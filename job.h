#pragma once
#define _GNU_SOURCE     // CPU_SET(3), <sched.h>
#include <sched.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>


typedef enum{                                PKG_ENERGY,   PP0_ENERGY,   PP1_ENERGY,   DRAM_ENERGY,   CORE_THERMAL,   PKG_THERMAL,   FREQUENCY, } poll_t;
static const char * const polltype2str[] = {"PKG_ENERGY", "PP0_ENERGY", "PP1_ENERGY", "DRAM_ENERGY", "CORE_THERMAL", "PKG_THERMAL", "FREQUENCY" };

typedef enum{                                      SPIN,   XRSTOR, } benchmark_t;
static const char * const benchmarktype2str[] = { "SPIN", "XRSTOR" };

typedef enum{                                        FIXED_FUNCTION_COUNTERS,   ENERGY_COUNTERS, NUM_LONGITUDINAL_FUNCTIONS, } longitudinal_t;
static const char * const longitudinaltype2str[] = {"FIXED_FUNCTION_COUNTERS", "ENERGY_COUNTERS"                             };

typedef enum{
    // For longitudinal recipes like fixed function performance counters, we want
    // the start and stop triggers to occur more-or-less simultaneously on all CPUs,
    // and we want those simultaneous events to happen more-or-less simultaneous
    // with the start of benchmarks and polling.  Thus, break out START and STOP
    // so they contain only the msr writes necessary for starting and stopping,
    // and put the other bookkeeping in SETUP and READ/TEARDOWN.  The latter might
    // become a single slot.
    SETUP,
    START,
    STOP,
    READ,
    TEARDOWN,
    NUM_LONGITUDINAL_EXECUTION_SLOTS
} longitudinal_slot_t;



struct poll_config{
    poll_t                      poll_type;
    cpu_set_t                   control_cpu;
    cpu_set_t                   polled_cpu;
    size_t                      total_ops;      // 1 cpu x 1024 polls/sec * expected seconds
    struct msr_batch_array      *poll_batches;
    struct msr_batch_op         *poll_ops;      // Each batch points to a single op (the POLL instruction)
    pthread_t                   poll_thread;
    pthread_mutex_t             poll_mutex;
};

struct benchmark_config{
    benchmark_t                 benchmark_type;
    cpu_set_t                   execution_cpus;
    char                        *benchmark_addr;    // For XRSTOR, points to the xrstor region
    uint64_t                    benchmark_param1;   // Moving to 128-bit granularity
    uint64_t                    benchmark_param2;   //  across two 64-bit unsigned ints.
    uint64_t                    executed_loops;
    pthread_t                   *benchmark_threads;
    pthread_mutex_t             *benchmark_mutexes;
    volatile bool               *halt;
};

struct longitudinal_config{
    longitudinal_t              longitudinal_type;
    cpu_set_t                   sample_cpus;

    size_t                      longitudinal_batch_count_per_type[ NUM_LONGITUDINAL_EXECUTION_SLOTS ];
    //struct msr_batch_array*     longitudinal_batches             [ NUM_LONGITUDINAL_EXECUTION_SLOTS ];

    struct msr_batch_array*     batches                              [ NUM_LONGITUDINAL_EXECUTION_SLOTS ];

};


struct job{
    cpu_set_t                   main_cpu;

    struct poll_config          **polls;
    size_t                      poll_count;         // The number of -p/--poll options parsed on the command line.

    struct benchmark_config     **benchmarks;
    size_t                      benchmark_count;    // The number of -b/--benchmark options parsed on the command line.

    struct longitudinal_config  **longitudinals;
    size_t                      longitudinal_count; // The number of -l/--longitudinal options parsed on the command line.

    struct timespec             duration;           // (seconds) main sleeps this long (nanosleep is thread-safe).
    uint64_t                    debug_level;
    volatile bool               halt;               // The big red off button.

};

