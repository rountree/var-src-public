#define _GNU_SOURCE     // CPU_SET(3) (affecting sched.h)
#include <errno.h>      // errno
#include <assert.h>     // discount error checking
#include <pthread.h>    // PTHREAD_MUTEX_INITIALIZER, pthread_mutex_[lock|unlock](3p)
#include <sched.h>      // cpu_set_t and friends, CPU_SETSIZE, sched_[get|set]affinity(2)
#include <getopt.h>     // getopt_long(3)
#include <stdlib.h>     // exit(3), malloc(3), random(3), srandom(3)
#include <stdio.h>      // printf(3)
#include <stdint.h>     // uint64_t, etc.
#include <inttypes.h>   // PRIu64, etc.
#include <sys/time.h>   // time_t, suseconds_t
#include <errno.h>      // errno
#include <string.h>     // strlen(3), strtok_r(3), strchr(3), strncmp(3)
#include <time.h>       // nanoleep(2)
#include <fcntl.h>      // open(2)
#include <unistd.h>     // close(2)
#include <sys/ioctl.h>  // ioctl(2)
#include "msr_safe.h"   // struct msr_batch_array, struct msr_batch_op, X86_IOC_MSR_BATCH
#include "msr_version.h" //MSR_SAFE_VERSION_u32
#include "spin.h"       // the spin benchmark
//
// Internal header files:
//
#include "job.h"                // Defines the job struct and the tasks it can contain.
#include "cpuset_utils.h"       // str2cpuset()
#include "int_utils.h"          // safe_strtoull()
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
        free( job.polls[i]->local_optarg );
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
    assert( 0 == sched_setaffinity( 0, sizeof( cpu_set_t ), &( job.polls[i]->control_cpu ) ) );
    int fd = open( "/dev/cpu/msr_batch", O_RDONLY );
    assert( -1 != fd );
    assert( 0 == pthread_mutex_lock( &(job.polls[i]->poll_mutex) ) );
    for( size_t b = 0; b < job.polls[i]->total_ops && !(job.halt); b++ ){
        errno = 0;
        int rc = ioctl( fd, X86_IOC_MSR_BATCH, &(job.polls[i]->poll_batches[b]) );
        job.polls[i]->poll_ops[b].tag = ( job.ab_selector << 1 ) | ( job.valid );
        job.valid = true;   // Set to false by the main thread, below, after A->B or B->A transition.
        if( -1 == rc ){
            fprintf( stderr, "%s:%d:%s ioctl in poll thread %zu batch %zu returned %d, errno=%d.\n",
                    __FILE__, __LINE__, __func__, i, b, rc, errno );
            perror("");
            exit(-1);
        }
        nanosleep( &job.polls[i]->interval, NULL );
    }
    close( fd );
    return 0;
}

void* benchmark_thread_start( void *v ){

    size_t benchmark_idx = (size_t)v;
    assert( 0 == sched_setaffinity( 0, sizeof( cpu_set_t ), &( job.benchmarks[ benchmark_idx ]->execution_cpu ) ) );
    assert( 0 == pthread_mutex_lock( &(job.benchmarks[ benchmark_idx ]->benchmark_mutex) ) );
    if( job.benchmarks[ benchmark_idx ]->benchmark_type == SPIN ){
        run_spin( job.benchmarks[ benchmark_idx ] );
    }else if( job.benchmarks[ benchmark_idx ]->benchmark_type == ABSHIFT ){
        run_abshift( job.benchmarks[ benchmark_idx ] );
    }
    return 0;
}

int main( int argc, char **argv ){

    srandom(13);
    setup_abxor();      // only needed for abxor.
    sizeof_check();
    parse_options( argc, argv, &job );
    populate_allowlist();
    setup_msrsafe_batches( &job );
    // Pin the main thread to the cpu requested.
    assert( 0 == sched_setaffinity( 0, sizeof( cpu_set_t ), &( job.main_cpu) ) );

    // Benchmark thread initialization
    for( uint64_t i = 0; i < job.benchmark_count; i++ ){

        // Point to the global halt and ab_selector variables
        job.benchmarks[i]->halt        = &job.halt;
        job.benchmarks[i]->ab_selector = &job.ab_selector;

        // Set up each thread.
        assert( 0 == pthread_mutex_init( &(job.benchmarks[i]->benchmark_mutex), NULL ) );
        assert( 0 == pthread_mutex_lock( &(job.benchmarks[i]->benchmark_mutex) ) );
        assert( 0 == pthread_create(     &(job.benchmarks[i]->benchmark_thread), NULL, benchmark_thread_start, (void*)i ) );
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
        assert( 0 == pthread_mutex_unlock( &(job.benchmarks[i]->benchmark_mutex ) ) );
    }

    // Sleep (note nanosleep does not rely on signals and is safe for multithreaded use).
    struct timespec elapsed;
    elapsed.tv_sec  = 0;
    elapsed.tv_nsec = 0;;
    uint64_t iterations[64] = {0,0};
    do{
        if( job.ab_randomized ){
            bool next = random() & 0x1;
            // Don't invalidate the current poll if we're still doing the same benchmark workload.
            if( job.ab_selector != next ){
                job.ab_selector = next;
                job.valid = false;
            }
        }else{
            job.ab_selector = ! job.ab_selector;
            job.valid = false;
        }

        nanosleep( &(job.ab_duration), NULL );

        iterations[ job.ab_selector ]++;

        elapsed.tv_sec =
            job.ab_duration.tv_sec    + elapsed.tv_sec +
            ( job.ab_duration.tv_nsec + elapsed.tv_nsec > 999'999'999L ? 1 : 0 );
        elapsed.tv_nsec = ( job.ab_duration.tv_nsec + elapsed.tv_nsec ) % 999'999'999L;
    }while( elapsed.tv_sec < job.duration.tv_sec );

    // Ring the bell.
    job.halt = true;

    // Benchmark thread join
    for( uint32_t i = 0; i < job.benchmark_count; i++ ){
        assert( 0 == pthread_join( job.benchmarks[i]->benchmark_thread, NULL ) );
    }

    // Poll thread join
    for( size_t i = 0; i < job.poll_count; i++ ){
        assert( 0 == pthread_join( job.polls[i]->poll_thread, NULL ) );
    }

    //printf("# a|b iterations:  %"PRIu64", %"PRIu64".\n", iterations[0], iterations[1]); FIXME

    run_longitudinal_batches( &job, STOP );
    run_longitudinal_batches( &job, READ );
    dump_batches( &job );

    // Benchmark thread cleanup
    for( uint32_t i = 0; i < job.benchmark_count; i++ ){
        pthread_mutex_destroy( &(job.benchmarks[i]->benchmark_mutex) );
    }

    run_longitudinal_batches( &job, TEARDOWN );
    teardown_msrsafe_batches( &job );
    cleanup();
}
