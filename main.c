#define _GNU_SOURCE     // CPU_SET(3) (affecting sched.h)
#include <errno.h>      // errno
#include <assert.h>     // discount error checking
#include <pthread.h>    // PTHREAD_MUTEX_INITIALIZER, pthread_mutex_[lock|unlock](3p)
#include <sched.h>      // cpu_set_t and friends, CPU_SETSIZE, sched_[get|set]affinity(2)
#include <getopt.h>     // getopt_long(3)
#include <stdlib.h>     // exit(3), malloc(3)
#include <stdio.h>      // printf(3)
#include <stdint.h>     // uint64_t, etc.
#include <inttypes.h>   // PRIu64, etc.
#include <assert.h>     // assert(3)
#include <sys/time.h>   // time_t, suseconds_t
#include <errno.h>      // errno
#include <string.h>     // strlen(3), strtok_r(3), strchr(3), strncmp(3)
#include <time.h>       // nanoleep(2)
#include <fcntl.h>      // open(2)
#include <unistd.h>     // close(2)
#include <sys/ioctl.h>  // ioctl(2)
#include "msr_safe.h"   // struct msr_batch_array, struct msr_batch_op, X86_IOC_MSR_BATCH
#include "msr_version.h" //MSR_SAFE_VERSION_u32
#include "xrstor.h"     // the xrstor benchmark
//
// Internal header files:
//
#include "job.h"                // Defines the job struct and the tasks it can contain.
#include "cpuset_utils.h"       // str2cpuset()
#include "string_utils.h"       // safe_strtoull()
#include "msr_utils.h"          // setup_msrsafe_batches()
#include "options.h"            // parse_options()

static void sizeof_check( void ){
        assert( 4 == sizeof( int ) );
        assert( 8 == sizeof( long ) ) ;
        assert( 8 == sizeof( long long ) ) ;
        assert( 8 == sizeof( size_t ) ) ;
        assert( 8 == sizeof( time_t ) ) ;
        assert( 8 == sizeof( suseconds_t ) ) ;
}
static struct job job;

static void cleanup( void ){
    for( size_t i = 0; i < job.poll_count; i++ ){
        free( job.polls[i] );
        job.polls[i] = NULL;
    }
    free( job.polls );
    job.polls = NULL;

    // benchmarks
    for( size_t i = 0; i < job.benchmark_count; i++ ){
        free( job.benchmarks[i] );
    }
    free( job.benchmarks );
    job.benchmarks = NULL;

    // longitudinals
    for( size_t i = 0; i < job.longitudinal_count; i++ ){
        free( job.longitudinals[i] );
        job.longitudinals[i] = NULL;
    }
    free( job.longitudinals );
    job.longitudinals = NULL;
}

void* poll_thread_start( void *v ){

    // Polls are easier than benchmarks, as there is only a single thread per
    // poll task.

    size_t i = (size_t)v;
    uint64_t cumulative_adjustment = 0;
    constexpr const uint64_t rollover_adjustment = 1ULL << 32;
    assert( 0 == sched_setaffinity( 0, sizeof( cpu_set_t ), &( job.polls[i]->control_cpu ) ) );
    int fd = open( "/dev/cpu/msr_batch", O_RDONLY );
    assert( -1 != fd );
    assert( 0 == pthread_mutex_lock( &(job.polls[i]->poll_mutex) ) );
    for( size_t b = 0; b < job.polls[i]->total_ops && !(job.halt); b++ ){
        errno = 0;
        int rc = ioctl( fd, X86_IOC_MSR_BATCH, &(job.polls[i]->poll_batches[b]) );
        if( -1 == rc ){
            fprintf( stderr, "%s:%d:%s ioctl in poll thread %zu batch %zu returned %d, errno=%d.\n",
                    __FILE__, __LINE__, __func__, i, b, rc, errno );
            perror("");
            exit(-1);
        }
        if( job.polls[i]->poll_type == POWER ){
            // Handle the rollover case here so we don't have to reinvent solutions
            // in the analysis phase.
            job.polls[i]->poll_ops[b].msrdata  += cumulative_adjustment;
            job.polls[i]->poll_ops[b].msrdata2 += cumulative_adjustment;

            // 1. Check to see if rollover happened within a poll op.
            if( job.polls[i]->poll_ops[b].msrdata2 < job.polls[i]->poll_ops[b].msrdata ){
                cumulative_adjustment += rollover_adjustment;
                job.polls[i]->poll_ops[b].msrdata2 += rollover_adjustment;
            // 2. Cehck to see if rollover happened between ops.
            }else if( (b > 0) && (job.polls[i]->poll_ops[b].msrdata < job.polls[i]->poll_ops[b-1].msrdata2) ){
                job.polls[i]->poll_ops[b].msrdata  += rollover_adjustment;
                job.polls[i]->poll_ops[b].msrdata2 += rollover_adjustment;
                cumulative_adjustment += rollover_adjustment;
            }
        }

    }
    close( fd );
    return 0;
}

void* benchmark_thread_start( void *v ){
    // The benchmark thread needs to know which job.benchmarks[?] it's working
    // on.  If there is more than one elegible cpu in the execution_cpus cpu_set_t,
    // it also needs to know the index to use for the mutex array.  Since we're
    // on machines with relatively small numbers of cores, we'll rely on the fact
    // that job is declared above in this translation unit and stuff the two
    // indexes into our single 64-bit parameter.  There are better ways, but this
    // codes quickly.
    //
    // Each thread in a benchmarks[x] uses the same execution_cpus cpu_set_t.
    // For now we'll rely on reasonable kernel scheduling.  Might come back later
    // and pin each thread to a single cpu.

    size_t i = (size_t)(((uint64_t)v) >> 32);
    size_t t = (size_t)(((uint64_t)v) & 0x00000000FFFFFFFFULL );
    assert( 0 == sched_setaffinity( 0, sizeof( cpu_set_t ), &( job.benchmarks[i]->execution_cpus ) ) );
    assert( 0 == pthread_mutex_lock( &(job.benchmarks[i]->benchmark_mutexes[t]) ) );
    if( job.benchmarks[ i ]->benchmark_type == XRSTOR ){
        run_xrstor( job.benchmarks[ i ] );
    }
    return 0;
}

int main( int argc, char **argv ){

    sizeof_check();
    parse_options( argc, argv, &job );
    populate_allowlist();
    setup_msrsafe_batches( &job );
    test_xrstor( );

    // Benchmark initialization
    for( size_t i = 0; i < job.benchmark_count; i++ ){
        if( job.benchmarks[ i ]->benchmark_type == XRSTOR ){
            initialize_xrstor( job.benchmarks[ i ] );

        }
    }

    // Pin the main thread to the cpu requested.
    assert( 0 == sched_setaffinity( 0, sizeof( cpu_set_t ), &( job.main_cpu) ) );

    // Benchmark thread initialization
    for( uint32_t i = 0; i < job.benchmark_count; i++ ){
        // Get the thread count via the execution_cpus cpu_set_t.
        size_t nthreads = (uint32_t)CPU_COUNT( &(job.benchmarks[i]->execution_cpus) );

        // Point to the global halt variable
        job.benchmarks[i]->halt = &job.halt;

        // Allocate space for nthreads pthread_t and pthread_mutex_t.
        job.benchmarks[i]->benchmark_threads = calloc( nthreads, sizeof( pthread_t ) );
        assert( job.benchmarks[i]->benchmark_threads );
        job.benchmarks[i]->benchmark_mutexes = calloc( nthreads, sizeof( pthread_mutex_t ) );
        assert( job.benchmarks[i]->benchmark_mutexes );

        // Set up each thread.
        for( uint32_t t = 0; t < nthreads; t++ ){
            assert( 0 == pthread_mutex_init( &(job.benchmarks[i]->benchmark_mutexes[t]), NULL ) );
            assert( 0 == pthread_mutex_lock( &(job.benchmarks[i]->benchmark_mutexes[t]) ) );
            assert( 0 == pthread_create(     &(job.benchmarks[i]->benchmark_threads[t]),
                    NULL, benchmark_thread_start, (void*)( (((uint64_t)i) << 32) | t ) ) );
        }
    }

    // Poll thread initialization
    for( size_t i = 0; i < job.poll_count; i++ ){

        // Set up the single thread in each poll
        assert( 0 == pthread_mutex_init( &(job.polls[i]->poll_mutex), NULL ) );
        assert( 0 == pthread_mutex_lock( &(job.polls[i]->poll_mutex) ) );
        assert( 0 == pthread_create(     &(job.polls[i]->poll_thread),
                    NULL, poll_thread_start, (void*)i ) );
    }

    run_longitudinal_batches( &job, SETUP );
    run_longitudinal_batches( &job, START );

    // Poll thread start
    for( size_t i = 0; i < job.poll_count; i++ ){
        assert( 0 == pthread_mutex_unlock( &(job.polls[i]->poll_mutex ) ) );
    }

    // Benchmark thread start
    for( uint32_t i = 0; i < job.benchmark_count; i++ ){
        for( uint32_t t = 0; t < (uint32_t)CPU_COUNT( &(job.benchmarks[i]->execution_cpus) ); t++ ){
            assert( 0 == pthread_mutex_unlock( &(job.benchmarks[i]->benchmark_mutexes[t]) ) );
        }
    }

    // Sleep (note nanosleep does not rely on signals and is safe for multithreaded use).
    nanosleep( &(job.duration), NULL );

    // Ring the bell.
    job.halt = true;

    // Benchmark thread join
    for( uint32_t i = 0; i < job.benchmark_count; i++ ){
        for( uint32_t t = 0; t < (uint32_t)CPU_COUNT( &(job.benchmarks[i]->execution_cpus) ); t++ ){
            assert( 0 == pthread_join( job.benchmarks[i]->benchmark_threads[t], NULL ) );
        }
    }

    // Poll thread join
    for( size_t i = 0; i < job.poll_count; i++ ){
        assert( 0 == pthread_join( job.polls[i]->poll_thread, NULL ) );
    }

    run_longitudinal_batches( &job, STOP );
    run_longitudinal_batches( &job, READ );
    dump_batches( &job );
    dump_xrstor( &job );

    // Benchmark thread cleanup
    for( uint32_t i = 0; i < job.benchmark_count; i++ ){
        for( uint32_t t = 0; t < (uint32_t)CPU_COUNT( &(job.benchmarks[i]->execution_cpus) ); t++ ){
            pthread_mutex_destroy( &(job.benchmarks[i]->benchmark_mutexes[t]) );
        }
        if( job.benchmarks[i]->benchmark_type == XRSTOR ){
            cleanup_xrstor( job.benchmarks[i] );
        }
        free( job.benchmarks[i]->benchmark_threads );
        free( job.benchmarks[i]->benchmark_mutexes );
    }

    run_longitudinal_batches( &job, TEARDOWN );
    teardown_msrsafe_batches( &job );
    cleanup();
}
