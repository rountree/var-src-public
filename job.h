#pragma once
#define _GNU_SOURCE     // CPU_SET(3), <sched.h>
#include <sched.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

typedef enum{                                      SPIN,   ABSHIFT,   ABXOR  } benchmark_t;
static const char * const benchmarktype2str[] = { "SPIN", "ABSHIFT", "ABXOR" };

typedef enum{                                        FIXED_FUNCTION_COUNTERS,   ALL_ALLOWED, NUM_LONGITUDINAL_FUNCTIONS, } longitudinal_t;
static const char * const longitudinaltype2str[] = {"FIXED_FUNCTION_COUNTERS", "ALL_ALLOWED"                             };

typedef enum{
    // For longitudinal recipes like fixed function performance counters, we want
    // the start and stop triggers to occur more-or-less simultaneously on all CPUs,
    // and we want those simultaneous events to happen more-or-less simultaneous
    // with the start of benchmarks and polling.  Thus, break out START and STOP
    // so they contain only the msr writes necessary for starting and stopping,
    // and put the other bookkeeping in SETUP and READ/TEARDOWN.  The latter might
    // become a single slot.
                                                     SETUP,   START,   STOP,   READ,   TEARDOWN, NUM_LONGITUDINAL_EXECUTION_SLOTS } longitudinal_slot_t;
static const char * const longitudinalslot2str[] = {"SETUP", "START", "STOP", "READ", "TEARDOWN"                                  };


struct poll_config{
    char *                      local_optarg;
    uint32_t                    msr;
    uint16_t                    flags;
    struct timespec             interval;
    cpu_set_t                   control_cpu;
    cpu_set_t                   polled_cpu;
    size_t                      total_ops;      // 1 cpu x 1024 polls/sec * expected seconds
    struct msr_batch_array      *poll_batches;
    struct msr_batch_op         *poll_ops;      // Each batch points to a single op (the POLL instruction)
    pthread_t                   poll_thread;
    pthread_mutex_t             poll_mutex;

    // The idea here is that we want to capture the current "encrypted" output at each
    // sample without using synchronization.  All benchmark threads will be moving their
    // "encrypted" value into their own "single_output" field; the polling thread will just
    // be aware of one of them.  At each sample, the polling thread will copy the contents of
    // single_output into the benchmark_output array, using the same index for polling samples.
    uint64_t                    key;                    // Initialized to NULL by poll, set by benchmark if needed.
    uint64_t                    *key_ptr;               //  "
    uint64_t                    *single_output_ptr;     //  "
    uint64_t                    *benchmark_output;      //  "

};

struct benchmark_config{
    // NOTE:  There is a benchmark config per benchmark per thread.
    benchmark_t                 benchmark_type;
    cpu_set_t                   execution_cpu;
    uint64_t                    benchmark_param1;
    uint64_t                    benchmark_param2;
    uint64_t                    benchmark_param3;
    uint64_t                    executed_loops[2];
    pthread_t                   benchmark_thread;
    pthread_mutex_t             benchmark_mutex;
    volatile bool               *halt;
    volatile bool               *ab_selector;   // See notes in struct job.

    uint64_t                    key;
    uint64_t                    single_output;
};

struct longitudinal_config{
    longitudinal_t              longitudinal_type;
    cpu_set_t                   sample_cpus;

    size_t                      longitudinal_batch_count_per_type[ NUM_LONGITUDINAL_EXECUTION_SLOTS ];
    //struct msr_batch_array*     longitudinal_batches             [ NUM_LONGITUDINAL_EXECUTION_SLOTS ];

    struct msr_batch_array*     batches                              [ NUM_LONGITUDINAL_EXECUTION_SLOTS ];

};


struct job{

    // Job
    cpu_set_t                   main_cpu;
    struct timespec             duration;           // (seconds:nanoseconds) main sleeps this long (nanosleep is thread-safe).
    bool                        ab_randomized;      // If true, randomly select whether to run A or B next.  False alternates.
    struct timespec             ab_duration;        // (seconds:nanoseconds) how long each A|B instance executes.

    // Internal
    volatile bool               halt;               // The big red off button.
    volatile bool               ab_selector;        // Select whether we're running workload A or B
                                                    //   WRITTEN TO by the main thread.
                                                    //   READ BY the benchmark thread and the polling thread.
    volatile bool               valid;              // Set invalid during A->B or B->A transition, as the
                                                    //   polling sample will straddle portions of both.
                                                    //   WRITTEN TO by the main thread and the polling thread.
                                                    //   READ BY the polling thread


    // Polls
    struct poll_config          **polls;
    size_t                      poll_count;         // The number of -p/--poll options parsed on the command line.

    // Benchmarks
    struct benchmark_config     **benchmarks;
    size_t                      benchmark_count;    // The number of -b/--benchmark options parsed on the command line.

    // Longitudinals
    struct longitudinal_config  **longitudinals;
    size_t                      longitudinal_count; // The number of -l/--longitudinal options parsed on the command line.


};

