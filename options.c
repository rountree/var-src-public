#define _GNU_SOURCE
#include <assert.h>
#include <getopt.h>             // getopt_long(3)
#include <stdlib.h>             // calloc(3), malloc(3)
#include <stdio.h>              // printf(3)
#include <string.h>             // strtok_r(3), strdup(3)
#include <stdint.h>
#include <inttypes.h>
#include "job.h"
#include "int_utils.h"          // safe_strtoull()
#include "cpuset_utils.h"       // str2cpuset()
#include "version.h"
#include "msr_utils.h"
#include "timespec_utils.h"

static void print_help( void ){
    printf( "var [options]\n" );
    printf( "  Version %"PRIu64".\n", var_version );
    printf( "  Built %s, %s.\n", __DATE__, __TIME__ );
    printf( "\n");
    printf( "Options:\n" );
    printf( "  -h / --help      Print this message and exit.\n");
    printf( "  -v / --version   Print version information and exit.\n");
    printf( "\n");
    printf( "  -t / --time=<timespec> (Default is 10 seconds)\n");
    printf( "\n");
    printf( "  -m / --main=<main_cpu>\n");
    printf( "  -b / --benchmark=<benchmark_type>:<execution_cpus>:<param1>:<param2>:<param3>\n");
    printf( "  -l / --longitudinal=<longitudinal_type>:<sample_cpus>\n");
    printf( "  -p / --poll=<msr_address>:<flags>:<timespec>:<control_cpu>:<sample_cpu>\n");
    printf( "\n");
    printf( "  -R / --abRandomized (enables random a|b selection)\n");
    printf( "  -T / --abTime=<timespec> (default is 1 second)\n");
    printf( "\n");
    printf( "The available benchmarks are SPIN, and ABSHIFT.\n");
    printf( "\n");
    printf( "The <longitudinal_type> may be either\n");
    printf( "  FIXED_FUNCTION_COUNTERS\n");
    printf( "    The counter accumulators for instructions retired, reference\n");
    printf( "    cycles, and cycle counts will be zeroed out before the start\n");
    printf( "    of the benchmark(s) and read out after <duration> seconds\n");
    printf( "    elapse.\n");
    printf( "  ENERGY_COUNTERS\n");
    printf( "    All possible counters are read, but only a subset will be present\n");
    printf( "    on any particular processor.  Check the error field for whether or\n");
    printf( "    not a reading is meaningful.  All values are package-scope.\n");
    printf( "\n");
    printf( "The several <cpu> fields expect CPU numbering of the type used by\n");
    printf( "  sched_setaffinity(2).  These can take the form of a single integer,\n");
    printf( "  or comma-separate single integers and ranges of integers m-n, where\n");
    printf( "  m<n.  With the exception of the longitudinal <sample_cpus>, each\n");
    printf( "  cpu should be unique.  Ideally, <execution_cpus> and <sample_cpus>\n");
    printf( "  should take up all CPUs on an isolated socket, while each\n");
    printf( "  <control_cpu> and the single <main_cpu> share a socket with, say,\n");
    printf( "  operating system background tasks.\n");
    printf( "\n");
    printf( "A <timespec> is a non-negative integer following by an optional suffix,\n");
    printf( "  \"ns\", \"us\", \"ms\", \"s\", \"m\", or \"h\", corresponding to\n");
    printf( "  to nanoseconds, microseconds, milliseconds, seconds, minutes, or hours,\n");
    printf( "  respectively.  The absence of a suffix implies seconds.\n");
    printf( "\n");
    printf( "The <flags> field can be any of the following, possibly OR'd together with '+'.\n");
    printf( "\n");
    for( uint16_t i = 1; i < MAX_OP_VAL; ){
        printf("\t");
        for( size_t j = 0; j < 4 && i < MAX_OP_VAL; j++, i *= 2 ){
            printf("%-15s", opflags2str[ i ]);
        }
        printf("\n");
    }
    printf( "\n");
    printf( "  For example, polling a register and capturing the core and processor\n");
    printf( "  temperature would be expressed by OP_POLL+OP_THERM+OP_PTHERM.\n");
}

static void print_options( int argc, char **argv, struct job *job ){

    FILE *fp = fopen( "job.out", "w" );
    assert( NULL != fp );

    // command line
    fprintf( fp, "# Command line:\n" );
    fprintf( fp, "#\t" );
    for( int i = 0; i < argc; i++ ){
        fprintf( fp, " %s", argv[i] );
    }
    fprintf( fp, "\n#\n" );

    // main_cpu
    fprintf(        fp, "#\t%-20s", "main_cpu: " );
    fprintf_cpuset( fp, &job->main_cpu );
    fprintf(        fp, "\n");

    // duration
    fprintf(          fp, "#\t%-20s", "duration: " );
    fprintf_timespec( fp, &job->duration );
    fprintf(          fp, "\n");

    // a|b randomized
    fprintf( fp, "#\t%-20s%s\n", "a|b randomized: ", job->ab_randomized ? "True" : "False" );

    // a|b duration
    fprintf(          fp, "#\t%-20s", "a|b duration: " );
    fprintf_timespec( fp, &job->ab_duration );
    fprintf(          fp, "\n#\n");

    // counts
    fprintf( fp, "# %zu %s, %zu %s, %zu %s.\n#\n",
            job->poll_count,
            job->poll_count == 1 ? "poll" : "polls",
            job->benchmark_count,
            job->benchmark_count == 1 ? "benchmark" : "benchmarks",
            job->longitudinal_count,
            job->longitudinal_count == 1 ? "longitudinal" : "longitudinals");

    // polls
    for( size_t i = 0; i < job->poll_count; i++ ){
        fprintf(          fp, "# poll %zu of %zu\n", i+1, job->poll_count);

        fprintf(          fp, "#\t%-15s", "sample cpu: ");
        fprintf_cpuset(    fp, &job->polls[i]->polled_cpu );
        fprintf(          fp, "\n" );

        fprintf(          fp, "#\t%-15s", "control cpu: ");
        fprintf_cpuset(    fp, &job->polls[i]->control_cpu );
        fprintf(          fp, "\n" );

        fprintf(          fp, "#\t%-15s%#"PRIx32"\n", "msr: ", job->polls[i]->msr );

        fprintf(          fp, "#\t%-15s", "flags: ");
        fprintf_flags(    fp, job->polls[i]->flags );
        fprintf(          fp, "\n");

        fprintf(          fp, "#\t%-15s", "interval: ");
        fprintf_timespec( fp, &job->polls[i]->interval );
        fprintf(          fp, "\n");
    }

    fprintf( fp, "#\n" );

    // benchmarks
    for( size_t i = 0; i < job->benchmark_count; i++ ){
        fprintf( fp, "# benchmark %zu of %zu:  type=%s. parameters=%#"PRIx64", %#"PRIx64", %#"PRIx64".\n",
                i+1, job->benchmark_count, benchmarktype2str[ job->benchmarks[i]->benchmark_type ],
                job->benchmarks[i]->benchmark_param1,
                job->benchmarks[i]->benchmark_param2,
                job->benchmarks[i]->benchmark_param3 );
        fprintf( fp, "#\texecution cpu:  ");
        fprintf_cpuset( fp, &job->benchmarks[i]->execution_cpu );
        fprintf( fp, "\n" );
    }

    fprintf( fp, "#\n" );

    // longitudinals
    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        fprintf(fp, "# longitudinal %zu of %zu:  type=%s.\n",
                i, job->longitudinal_count, longitudinaltype2str[ job->longitudinals[i]->longitudinal_type ]);
        fprintf(fp, "#\tsample cpu:  ");
        fprintf_cpuset( fp, &job->longitudinals[i]->sample_cpus );
        fprintf(fp, "\n");
    }
    fclose(fp);
}

void parse_options( int argc, char **argv, struct job *job ){
    // Default values:
    job->duration.tv_sec     = 10;
    job->duration.tv_nsec    =  0;
    job->ab_randomized       =  false;
    job->ab_duration.tv_sec  =  1;
    job->ab_duration.tv_nsec =  0;

    static struct option long_options[] = {
        { .name = "benchmark",    .has_arg = required_argument, .flag = NULL, .val = 'b' },
        { .name = "help",         .has_arg = no_argument,       .flag = NULL, .val = 'h' },
        { .name = "longitudinal", .has_arg = required_argument, .flag = NULL, .val = 'l' },
        { .name = "main",         .has_arg = required_argument, .flag = NULL, .val = 'm' },
        { .name = "poll",         .has_arg = required_argument, .flag = NULL, .val = 'p' },
        { .name = "time",         .has_arg = required_argument, .flag = NULL, .val = 't' },
        { .name = "version",      .has_arg = no_argument,       .flag = NULL, .val = 'v' },
        { .name = "abTime",       .has_arg = required_argument, .flag = NULL, .val = 'T' },
        { .name = "abRandomize",  .has_arg = no_argument,       .flag = NULL, .val = 'R' },
        { 0, 0, 0, 0}
    };

    while(1){
        int c = getopt_long( argc, argv, ":RT:b:d:hl:m:p:t:v", long_options, NULL );
        if( -1 == c ){
            break;
        }
        switch( c ){
            default:
                printf(" %s:%d:%s optopt='%c' No idea what just happened.  Don't do that.\n",
                        __FILE__, __LINE__, __func__, optopt );
                exit(-1);
                break;
            case '?':
                printf(" %s:%d:%s optopt='%c' Unknown options, ambiguous match, or extraneous parameter. \n",
                        __FILE__, __LINE__, __func__, optopt );
                exit(-1);
                break;
            case ':':
                printf(" %s:%d:%s optopt='%c' Missing argument.  Hope that helped.\n",
                        __FILE__, __LINE__, __func__, optopt);
                exit(-1);
                break;
            case 'T':     // a|b duration
            {
                char *local_optarg = strdup( optarg );
                str2timespec( local_optarg, &job->ab_duration );
                free( local_optarg );
                break;
            }
            case 'R':
                job->ab_randomized = true;
                break;
            case 't':   // time (duration)
            {
                char *local_optarg = strdup( optarg );
                str2timespec( local_optarg, &(job->duration) );
                free( local_optarg );
                break;
            }
            case 'm':   // main
            {
                str2cpuset( optarg, &job->main_cpu );
                break;
            }
            case 'l':   // longitudinal
            {
                // Increment the number of longitudinal tasks.
                job->longitudinal_count++;

                // Increase the size of the array of longitudinal tasks
                job->longitudinals = realloc(
                        job->longitudinals,
                        sizeof( struct longitudinal_config *) * job->longitudinal_count );
                assert( job->longitudinals );

                // Allocate a longitudinal_config struct.
                struct longitudinal_config *lng = calloc( 1, sizeof( struct longitudinal_config ) );
                assert( lng );
                job->longitudinals[ job->longitudinal_count - 1] = lng;

                // Parse optarg
                char *local_optarg = malloc( strlen(optarg) + 1 );
                assert( local_optarg );
                strcpy( local_optarg, optarg );
                char *saveptr = NULL;
                char *lng_type = strtok_r( local_optarg, ":", &saveptr);
                char *lng_cpuset = strtok_r( NULL, ":", &saveptr );
                char *should_be_null = strtok_r( NULL, ":", &saveptr);
                if( NULL == lng_cpuset ){
                    printf( "%s:%d:%s Parameter (%s) to -l/--longitudinal is missing <sample_cpus>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit( -1 ) ;
                }
                if( NULL != should_be_null ){
                    printf( "%s:%d:%s Extra parameters in -l/--longitudinal (%s).\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }

                // Fill in the struct
                if( 0 == strcmp( longitudinaltype2str[0], lng_type ) ){
                    lng->longitudinal_type = FIXED_FUNCTION_COUNTERS;
                }else if ( 0 == strcmp( longitudinaltype2str[1], lng_type ) ){
                    lng->longitudinal_type = ENERGY_COUNTERS;
                }else{
                    printf( "%s:%d:%s Unknown longitudinal type (%s).\n",
                            __FILE__, __LINE__, __func__, lng_type );
                    exit(-1);
                }
                str2cpuset( lng_cpuset, &lng->sample_cpus );

                free(local_optarg);
                break;
            }
            case 'b':   // benchmark
            {
                // Parse optarg
                char *local_optarg = strdup( optarg );
                char *saveptr = NULL;
                char *bch_type   = strtok_r( local_optarg, ":", &saveptr );
                char *bch_cpuset = strtok_r( NULL, ":", &saveptr );
                char *bch_param1  = strtok_r( NULL, ":", &saveptr );
                char *bch_param2  = strtok_r( NULL, ":", &saveptr );
                char *bch_param3  = strtok_r( NULL, ":", &saveptr );
                char *should_be_null = strtok_r( NULL, ":", &saveptr );
                if( NULL == bch_cpuset ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--benchmark missing <execution_cpus>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit( -1 ) ;
                }
                if( NULL == bch_param1 ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--benchmark missing <param1>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }
                if( NULL == bch_param2 ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--benchmark missing <param2>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }
                if( NULL == bch_param3 ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--benchmark missing <param3>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }
                if( NULL != should_be_null ){
                    printf( "%s:%d:%s Extra parameters in -b/--benchmark (%s).\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }

                // Unlike longitudinal tasks, we need one benchmark_config per thread.
                cpu_set_t all_cpus;
                str2cpuset( bch_cpuset, &all_cpus );
                size_t num_threads = get_cpuset_count( &all_cpus );

                unsigned int current_cpu = 0;
                size_t first_new_benchmark_idx = job->benchmark_count;
                job->benchmark_count += num_threads;

                // Increase the size of the array of benchmark pointers
                job->benchmarks = realloc(
                        job->benchmarks,
                        sizeof( struct benchmark_config *) * job->benchmark_count );
                assert( job->benchmarks );

                // Grab the parameters
                uint64_t benchmark_param1 = safe_strtoull( bch_param1 );
                uint64_t benchmark_param2 = safe_strtoull( bch_param2 );
                uint64_t benchmark_param3 = safe_strtoull( bch_param3 );

                // Allocate and fill in the structs.
                for( size_t bch_idx = first_new_benchmark_idx; bch_idx < job->benchmark_count; bch_idx++ ){

                    // Allocation
                    job->benchmarks[ bch_idx ] = calloc( 1, sizeof( struct benchmark_config ) );
                    assert( job->benchmarks[ bch_idx ] );

                    // Benchmark type
                    if( 0 == strcmp( benchmarktype2str[SPIN], bch_type ) ){
                        job->benchmarks[ bch_idx ]->benchmark_type = SPIN;
                    }else if( 0 == strcmp( benchmarktype2str[ABSHIFT], bch_type ) ){
                        job->benchmarks[ bch_idx ]->benchmark_type = ABSHIFT;
                    }else{
                        printf( "%s:%d:%s Unknown benchmark type (%s).\n",
                                __FILE__, __LINE__, __func__, bch_type );
                        exit(-1);
                    }

                    // cpu
                    current_cpu = get_next_cpu( current_cpu, 255, &all_cpus );
                    cpu2cpuset( current_cpu++, &(job->benchmarks[ bch_idx ]->execution_cpu) );

                    // Parameters
                    job->benchmarks[ bch_idx ]->benchmark_param1 = benchmark_param1;
                    job->benchmarks[ bch_idx ]->benchmark_param2 = benchmark_param2;
                    job->benchmarks[ bch_idx ]->benchmark_param3 = benchmark_param3;

                }

                free(local_optarg);
                break;
            }
            case 'p':   // poll
            {
                // Increment the number of poll tasks
                job->poll_count++;

                // Increase the size of the array of poll tasks
                job->polls = realloc(
                        job->polls,
                        sizeof( struct poll_config *) * job->poll_count );
                assert( job->polls );

                // Allocate a poll_config struct
                struct poll_config *pll = calloc( 1, sizeof( struct poll_config ) );
                assert( pll );
                job->polls[ job->poll_count - 1 ] = pll;

                // Parse optarg
                pll->local_optarg = strdup( optarg );   // Save this one for describing output.
                char *local_optarg = strdup( optarg );  // Let strtok_r() chew on this one.
                char *saveptr = NULL;

                char *pll_msr_str               = strtok_r( local_optarg, ":", &saveptr);
                char *pll_flags_str             = strtok_r( NULL, ":", &saveptr );
                char *pll_timespec_str          = strtok_r( NULL, ":", &saveptr );
                char *pll_control_cpuset_str    = strtok_r( NULL, ":", &saveptr );
                char *pll_polled_cpuset_str     = strtok_r( NULL, ":", &saveptr );
                char *should_be_null            = strtok_r( NULL, ":", &saveptr);
                if( NULL == pll_msr_str ){
                    printf( "%s:%d:%s Parameter (%s) to -p/--poll missing required <msr_address>.\n",
                            __FILE__, __LINE__, __func__, pll->local_optarg );
                    exit( -1 );
                }
                if( NULL == pll_flags_str ){
                    printf( "%s:%d:%s Parameter (%s) to -p/--poll missing required <flags>.\n",
                            __FILE__, __LINE__, __func__, pll->local_optarg );
                    exit( -1 );
                }
                if( NULL == pll_timespec_str ){
                    printf( "%s:%d:%s Parameter (%s) to -p/--poll missing required <timespec>.\n",
                            __FILE__, __LINE__, __func__, pll->local_optarg );
                    exit( -1 );
                }
                if( NULL == pll_control_cpuset_str ){
                    printf( "%s:%d:%s Parameter (%s) to -p/--poll missing required <control_cpus>.\n",
                            __FILE__, __LINE__, __func__, pll->local_optarg );
                    exit( -1 ) ;
                }
                if( NULL == pll_polled_cpuset_str ){
                    printf( "%s:%d:%s Parameter (%s) to -p/--poll missing required <sample_cpu>.\n",
                            __FILE__, __LINE__, __func__, pll->local_optarg );
                    exit( -1 ) ;
                }
                if( NULL != should_be_null ){
                    printf( "%s:%d:%s Extra parameters in -p/--poll (%s).\n",
                            __FILE__, __LINE__, __func__, pll->local_optarg );
                    exit(-1);
                }

                // Fill in the struct
                pll->msr = (uint32_t)safe_strtoull( pll_msr_str );
                pll->flags = str2flags( pll_flags_str );
                str2timespec( pll_timespec_str, &pll->interval );
                str2cpuset( pll_polled_cpuset_str, &pll->polled_cpu );
                str2cpuset( pll_control_cpuset_str, &pll->control_cpu );
                free(local_optarg);
                break;
            }
            case 'v':   // version
                printf( "  Version %"PRIu64".\n", var_version );
                exit(0);
            case 'h':   // help
                print_help();
                exit(0);
                break;
        }; // switch
    };

    print_options( argc, argv, job );
}
