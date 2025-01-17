#pragma once
#define _GNU_SOURCE     // CPU_SET(3), <sched.h>
#include <sched.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>


typedef enum{
    POWER,
    THERMAL,
    FREQUENCY,
} poll_t;

typedef enum{
    SLEEP,
    SPIN,
    XRSTOR,
} benchmark_t;

typedef enum{
    FIXED_FUNCTION_COUNTERS,
} longitudinal_t;

typedef enum{
    SETUP,
    START,
    STOP,
    READ,
    TEARDOWN,
    NUM_LONGITUDINAL_BATCH_TYPES
} longitudinal_batch_t;


static const char * const polltype2str[] = {"POWER", "THERMAL", "FREQUENCY"};
static const char * const benchmarktype2str[] = {"SLEEP", "SPIN", "XRSTOR"};
static const char * const longitudinaltype2str[] = {"FIXED_FUNCTION_COUNTERS"};

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
    bool                        *halt;
};

struct longitudinal_config{
    // Indexing here gets a little.... aggressive.
    // for( i = 0; i < job->longitudinal_count; i++ ){
    //   for( j = 0; j < NUM_LONGITUDINAL_BATCH_TYPES; j++ ){
    //     for( k = 0; k < longitudinal_batch_count_per_type[ j ] ){
    //       for( o = 0; o < jobs->longitudinals[i]->longitudinal_batches[j][k].numops; o++ ){
    //         job->longitudinals[i]->longitudinal_batches[j][k].ops[o].cpu = cpu_idx;
    longitudinal_t              longitudinal_type;
    cpu_set_t                   sample_cpus;

    size_t                      longitudinal_batch_count_per_type[ NUM_LONGITUDINAL_BATCH_TYPES ];
    struct msr_batch_array*     longitudinal_batches             [ NUM_LONGITUDINAL_BATCH_TYPES ];

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
    bool                        halt;               // The big red off button.

};

