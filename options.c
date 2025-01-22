#define _GNU_SOURCE
#include <assert.h>
#include <getopt.h>             // getopt_long(3)
#include <stdlib.h>             // calloc(3), malloc(3)
#include <stdio.h>              // printf(3)
#include <string.h>             // strtok_r(3)
#include <stdint.h>
#include <inttypes.h>
#include "job.h"
#include "string_utils.h"       // safe_strtoull()
#include "cpuset_utils.h"       // str2cpuset()
#include "version.h"

static void print_help( void ){
    printf( "vanallin [options]\n" );
    printf( "  Version %"PRIu64".\n", vanallin_version );
    printf( "  Built %s, %s.\n", __DATE__, __TIME__ );
    printf( "\n");
    printf( "Options:\n" );
    printf( "  -h / --help      Print this message and exit.\n");
    printf( "  -v / --version   Print version information and exit.\n");
    printf( "  -d / --debug_level=<level> (Higher is more verbose, max is 3.)\n");
    printf( "\n");
    printf( "  -s / --seconds=<duration> (Default is 10 seconds)\n");
    printf( "\n");
    printf( "  -m / --main=<main_cpu>\n");
    printf( "  -b / --benchmark=<benchmark_type>:<execution_cpus>:<benchmark_param1>:<benchmark_param2\n");
    printf( "  -l / --longitudinal=<longitudinal_type>:<sample_cpus>\n");
    printf( "  -p / --poll=<poll_type>:<control_cpu>:<sample_cpu>\n");
    printf( "\n");
    printf( "The only (currently) valid benchmark is XRSTOR, which continuously\n");
    printf( "  loads the AVX registers with the contents of a prepared memory\n");
    printf( "  region.  The user may specify multiple <execution_cpus>, but note\n");
    printf( "  that AVX registers may be a per-core resource, rather than per cpu.\n");
    printf( "  <benchmark_paramN> are two 64-bit values that will be used to fill all\n");
    printf( "  the bits in every AVX register.  XRSTOR is implemented as a tight\n");
    printf( "  loop around the Intel Intrinsics _mm256_zeroall() and _xrstor64().\n");
    printf( "  The loop executes until <duration> seconds have expired.\n");
    printf( "\n");
    printf( "The two valid <longitudinal_type>s are FIXED_FUNCTION_COUNTERS\n");
    printf( "  and ENERGY_COUNTERS.  With regard to the former:\n");
    printf( "    The counter accumulators for instructions retired, reference\n");
    printf( "    cycles, and cycle counts will be zeroed out before the start\n");
    printf( "    of the benchmark(s) and read out after <duration> seconds\n");
    printf( "    elapse.\n");
    printf( "  With regard to the latter:\n");
    printf( "    All possible counters are read, but only a subset will be present\n");
    printf( "    on any particular processor.  Check the error field for whether or\n");
    printf( "    not a reading is meaningful.  All values are package-scope.\n");
    printf( "\n");
    printf( "Valid <poll_types> are POWER, THERMAL, and FREQUENCY.  The model-\n");
    printf( "  specific registers responsible for each are expected to be updated\n");
    printf( "  at 1kHz (once per millisecond, more or less).  Each sample contains\n");
    printf( "  the timestamped initial and changed register values.  If the value\n");
    printf( "  fails to change over roughly 10 milliseconds, the sample indicates\n");
    printf( "  the number of maximum reads was exceeded.\n");
    printf( "\n");
    printf( "The several <cpu> fields expect CPU numbering of the type used by\n");
    printf( "  sched_setaffinity(2).  These can take the form of a single integer,\n");
    printf( "  or comma-separate single integers and ranges of integers m-n, where\n");
    printf( "  m<n.  With the exception of the longitudinal <sample_cpus>, each\n");
    printf( "  cpu should be unique.  Ideally, <execution_cpus> and <sample_cpus>\n");
    printf( "  should take up all CPUs on an isolated socket, while each\n");
    printf( "  <control_cpu> and the single <main_cpu> share a socket with, say,\n");
    printf( "  operating system background tasks.\n");
}

static void print_parameters( struct job *job ){

    printf("# %s:%d:%s\n# %d main cpu(s):  ", __FILE__, __LINE__, __func__, CPU_COUNT( &job->main_cpu ) );
    print_cpuset( &job->main_cpu );
    printf("#\n");
    printf("# %zu polls, %zu benchmarks, %zu longitudinals.  duration = %ld.\n",
            job->poll_count, job->benchmark_count, job->longitudinal_count,
            job->duration.tv_sec );

    // polls
    for( size_t i = 0; i < job->poll_count; i++ ){
        printf("# poll %zu of %zu:  type=%s.\n",
                i, job->poll_count, polltype2str[ job->polls[i]->poll_type ]);
        printf("#          sample cpus:  ");
        print_cpuset( &job->polls[i]->polled_cpu );
        printf("#          control cpu:  ");
        print_cpuset( &job->polls[i]->control_cpu );
    }

    // benchmarks
    for( size_t i = 0; i < job->benchmark_count; i++ ){
        printf("# benchmark %zu of %zu:  type=%s. parameters=%#"PRIx64", %#"PRIx64".\n",
                i, job->benchmark_count, benchmarktype2str[ job->benchmarks[i]->benchmark_type ],
                job->benchmarks[i]->benchmark_param1, job->benchmarks[i]->benchmark_param2 );
        printf("#          execution cpus:  ");
        print_cpuset( &job->benchmarks[i]->execution_cpus );
    }

    // longitudinals
    for( size_t i = 0; i < job->longitudinal_count; i++ ){
        printf("# longitudinal %zu of %zu:  type=%s.\n",
                i, job->longitudinal_count, longitudinaltype2str[ job->longitudinals[i]->longitudinal_type ]);
        printf("#          sample cpus:  ");
        print_cpuset( &job->longitudinals[i]->sample_cpus );
    }
}

static void cpuset_checks( struct job *job ){
    // cpuset checks:  setup.  cpus isolated via the isolcpus boot parameter are still eligible
    // for sched_setaffinity.  cpus removed via taskset(1) are not eligible.
    cpu_set_t eligible_cpus, ANDed_cpus;
    assert( 0 == sched_getaffinity( 0, sizeof( cpu_set_t ), &eligible_cpus ) );

    // cpuset checks:  There is exactly one main cpu, and it must be eligible.
    if( 1 != CPU_COUNT( &job->main_cpu ) ){
        printf("Error:  Need to specify exactly one main cpu via -m <cpu> or --main=<cpu>\n");
        exit(-1);
    }
    CPU_AND( &ANDed_cpus, &eligible_cpus, &job->main_cpu );
    if( 1 != CPU_COUNT( &ANDed_cpus ) ){
        printf("Requested main cpu is not on the list of eligible cpus.\n");
        printf("Requested main cpu:  ");
        print_cpuset( &job->main_cpu );
        printf("Eligible cpus:  ");
        print_cpuset( &eligible_cpus );
        exit(-1);
    }

    // cpuset checks:  Each poll task has a single eligible control cpu and a single eligible polled cpu.
    for( size_t i = 0; i < job->poll_count; i++ ){
        if( 1 != CPU_COUNT( &(job->polls[i]->control_cpu) ) ){
            printf("Poll task #%zu must have exactly one control cpu.\n", i);
            exit(-1);
        }
        if( 1 != CPU_COUNT( &(job->polls[i]->polled_cpu) ) ){
            printf("Poll task #%zu must have exactly one polled cpu.\n", i);
        }
        CPU_AND( &ANDed_cpus, &eligible_cpus, &(job->polls[i]->control_cpu) );
        if( 1 != CPU_COUNT( &ANDed_cpus ) ){
            printf("The specified control cpu in not eligible.\n");
            printf("Requested control cpu:");
            print_cpuset( &(job->polls[i]->control_cpu) );
            printf("Eligible cpus:  ");
            print_cpuset( &eligible_cpus );
            exit(-1);
        }
        CPU_AND( &ANDed_cpus, &eligible_cpus, &(job->polls[i]->polled_cpu) );
        if( 1 != CPU_COUNT( &ANDed_cpus ) ){
            printf("The specified polled cpu in not eligible.\n");
            printf("Requested polled cpu:");
            print_cpuset( &(job->polls[i]->polled_cpu) );
            printf("Eligible cpus:  ");
            print_cpuset( &eligible_cpus );
            exit(-1);
        }
    }

    // cpuset checks:  Every execution task of each benchmark task is eligible.
    for( size_t i = 0; i < job->benchmark_count; i++ ){
        CPU_AND( &ANDed_cpus, &eligible_cpus, &(job->benchmarks[i]->execution_cpus) );
        if( CPU_COUNT( &ANDed_cpus ) != CPU_COUNT( &(job->benchmarks[i]->execution_cpus) ) ){
            printf("One or more of the specified execution cpus on benchmark task %zu is ineligible.\n", i);
            printf("Requested execution cpu(s):  ");
            print_cpuset( &(job->benchmarks[i]->execution_cpus ) );
            printf("Eligible cpus:  ");
            print_cpuset( &eligible_cpus );
            exit(-1);
        }

    }

    // cpuset checks:  Main, control, polled, and execution cpus must be unique across all polling and benchmark tasks.
    int allegedly_unique_cpus_count = 0;
    cpu_set_t allegedly_unique_cpus;
    CPU_ZERO( &allegedly_unique_cpus );

    CPU_OR( &allegedly_unique_cpus, &allegedly_unique_cpus, &job->main_cpu );
    allegedly_unique_cpus_count += CPU_COUNT( &job->main_cpu );

    for( size_t i = 0; i < job->poll_count; i++ ){
        CPU_OR( &allegedly_unique_cpus, &allegedly_unique_cpus, &(job->polls[i]->control_cpu) );
        allegedly_unique_cpus_count += CPU_COUNT( &(job->polls[i]->control_cpu) );
        CPU_OR( &allegedly_unique_cpus, &allegedly_unique_cpus, &(job->polls[i]->polled_cpu) );
        allegedly_unique_cpus_count += CPU_COUNT( &(job->polls[i]->polled_cpu) );
    }

    for( size_t i = 0; i < job->benchmark_count; i++ ){
        CPU_OR( &allegedly_unique_cpus, &allegedly_unique_cpus, &(job->benchmarks[i]->execution_cpus) );
        allegedly_unique_cpus_count += CPU_COUNT( &(job->benchmarks[i]->execution_cpus) );
    }

    if( CPU_COUNT( &allegedly_unique_cpus ) != allegedly_unique_cpus_count ){
        printf("There are one or more duplicate cpus among the main, control, polled, and execution cpus.\n");
        printf("Good luck!\n");
        exit(-1);
    }
}

void parse_options( int argc, char **argv, struct job *job ){
    // Default values:
    job->duration.tv_sec = 10;

    static struct option long_options[] = {
        { .name = "benchmark",    .has_arg = required_argument, .flag = NULL, .val = 'b' },
        { .name = "debug_level",  .has_arg = required_argument, .flag = NULL, .val = 'd' },
        { .name = "help",         .has_arg = no_argument,       .flag = NULL, .val = 'h' },
        { .name = "longitudinal", .has_arg = required_argument, .flag = NULL, .val = 'l' },
        { .name = "main",         .has_arg = required_argument, .flag = NULL, .val = 'm' },
        { .name = "poll",         .has_arg = required_argument, .flag = NULL, .val = 'p' },
        { .name = "seconds",      .has_arg = required_argument, .flag = NULL, .val = 's' },
        { .name = "version",      .has_arg = no_argument,       .flag = NULL, .val = 'v' },
        { 0, 0, 0, 0}
    };

    while(1){
        int c = getopt_long( argc, argv, ":b:d:hl:m:p:s:v", long_options, NULL );
        if( -1 == c ){
            break;
        }
        switch( c ){
            default:
                printf(" %s:%d:%s No idea what just happened.  Don't do that.\n",
                        __FILE__, __LINE__, __func__ );
                exit(-1);
                break;
            case '?':
                printf(" %s:%d:%s Unknown options, ambiguous match, or extraneous parameter. \n",
                        __FILE__, __LINE__, __func__ );
                exit(-1);
                break;
            case ':':
                printf(" %s:%d:%s Missing argument.  Hope that helped.\n",
                        __FILE__, __LINE__, __func__);
                exit(-1);
                break;
            case 'd':   // debug_level
                job->debug_level = safe_strtoull( optarg );
                break;
            case 's':   // seconds
                job->duration.tv_sec = safe_strtoull( optarg );
                break;
            case 'm':   // main
                str2cpuset( optarg, &job->main_cpu );
                break;
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
                    printf( "%s:%d:%s Parameter (%s) to -l/--longitudinal missing <sample_cpus>.\n",
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
                // Increment the number of benchmark tasks.
                job->benchmark_count++;

                // Increase the size of the array of benchmark pointers
                job->benchmarks = realloc(
                        job->benchmarks,
                        sizeof( struct benchmark_config *) * job->benchmark_count );
                assert( job->benchmarks );

                // Allocate a benchmark_config struct
                struct benchmark_config *bch = calloc( 1, sizeof( struct benchmark_config ) );
                assert( bch );
                job->benchmarks[ job->benchmark_count - 1 ] = bch;

                // Parse optarg
                char *local_optarg = malloc( strlen(optarg) + 1 );
                assert( local_optarg );
                strcpy( local_optarg, optarg );
                char *saveptr = NULL;
                char *bch_type   = strtok_r( local_optarg, ":", &saveptr );
                char *bch_cpuset = strtok_r( NULL, ":", &saveptr );
                char *bch_param1  = strtok_r( NULL, ":", &saveptr );
                char *bch_param2  = strtok_r( NULL, ":", &saveptr );
                char *should_be_null = strtok_r( NULL, ":", &saveptr );
                if( NULL == bch_cpuset ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--benchmark missing <execution_cpus>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit( -1 ) ;
                }
                if( NULL == bch_param1 ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--benchmark missing <benchmark_param1>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }
                if( NULL == bch_param2 ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--benchmark missing <benchmark_param2>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }
                if( NULL != should_be_null ){
                    printf( "%s:%d:%s Extra parameters in -b/--benchmark (%s).\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }

                // Fill in the struct
                if( 0 == strcmp( benchmarktype2str[XRSTOR], bch_type ) ){
                    bch->benchmark_type = XRSTOR;
                }else{
                    printf( "%s:%d:%s Unknown benchmark type (%s).\n",
                            __FILE__, __LINE__, __func__, bch_type );
                    exit(-1);
                }
                str2cpuset( bch_cpuset, &bch->execution_cpus );
                bch->benchmark_param1 = safe_strtoull( bch_param1 );
                bch->benchmark_param2 = safe_strtoull( bch_param2 );
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
                char *local_optarg = malloc( strlen(optarg) + 1 );
                assert( local_optarg );
                strcpy( local_optarg, optarg );
                char *saveptr = NULL;
                char *pll_type = strtok_r( local_optarg, ":", &saveptr);
                char *pll_control_cpuset = strtok_r( NULL, ":", &saveptr );
                char *pll_polled_cpuset = strtok_r( NULL, ":", &saveptr );
                char *should_be_null = strtok_r( NULL, ":", &saveptr);
                if( NULL == pll_control_cpuset ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--poll missing <control_cpus>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit( -1 ) ;
                }
                if( NULL == pll_polled_cpuset ){
                    printf( "%s:%d:%s Parameter (%s) to -b/--poll missing <sample_cpu>.\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit( -1 ) ;
                }
                if( NULL != should_be_null ){
                    printf( "%s:%d:%s Extra parameters in -b/--poll (%s).\n",
                            __FILE__, __LINE__, __func__, optarg);
                    exit(-1);
                }

                // Fill in the struct
                if( 0 == strcmp( polltype2str[POWER], pll_type ) ){
                    pll->poll_type = POWER;
                }else if( 0 == strcmp( polltype2str[THERMAL], pll_type ) ){
                    pll->poll_type = THERMAL;
                }else if( 0 == strcmp( polltype2str[FREQUENCY], pll_type ) ){
                    pll->poll_type = FREQUENCY;
                }else{
                    printf( "%s:%d:%s Unknown poll type (%s).\n",
                            __FILE__, __LINE__, __func__, pll_type );
                    exit(-1);
                }
                str2cpuset( pll_polled_cpuset, &pll->polled_cpu );
                str2cpuset( pll_control_cpuset, &pll->control_cpu );

                free(local_optarg);

                break;
            }
            case 'v':   // version
                printf( "  Version %"PRIu64".\n", vanallin_version );
                exit(0);
            case 'h':   // help
                print_help();
                break;
        }; // switch
    };

    cpuset_checks( job );

    print_parameters( job );

}
