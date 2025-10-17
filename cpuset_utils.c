#define _GNU_SOURCE
#include <stdlib.h>         // exit(3)
#include <assert.h>         // assert(3)
#include <stdio.h>          // printf(3)
#include <string.h>         // strtok_r(3), strchr(3)
#include "cpuset_utils.h"
#include "int_utils.h"      //

void cpu2cpuset( int cpu, cpu_set_t * const cpuset ){

    CPU_ZERO( cpuset );
    CPU_SET( cpu, cpuset );
}

size_t get_cpuset_count( cpu_set_t *cpuset ){
    return (size_t)CPU_COUNT( cpuset );
}

void str2cpuset( const char * const s, cpu_set_t *cpuset ){

    if( NULL == s ){
        printf( "%s:%d:%s Can't convert null pointer to a cpuset.\n",
                __FILE__, __LINE__, __func__);
        exit(-1);
    }else if( '\0' == *s ){
        printf( "%s:%d:%s Can't convert empty string to a cpuset.\n",
                __FILE__, __LINE__, __func__);
    }

    CPU_ZERO( cpuset );

    char * local_s = calloc( strlen(s) + 1, sizeof( char ) );
    assert( local_s );
    strcpy( local_s, s );
    char *comma_saveptr=NULL, *dash_location=NULL, *comma_str;
    unsigned long long lo_cpu, hi_cpu;
    while(1){
        // Find the next comma
        comma_str=strtok_r(
                (NULL == comma_saveptr) ? local_s : NULL,
                ",",
                &comma_saveptr);
        if( NULL == comma_str ){
            break; // no more tokens, go home.
        }
        // comma_str contains a token that is either <number> or <number>-<number>
        dash_location = strchr( comma_str, '-' );

        if( dash_location ){ // found a dash
            *dash_location = '\0';
            lo_cpu = safe_strtoull( comma_str );
            hi_cpu = safe_strtoull( dash_location + 1 );
            *dash_location = '-'; // leave it as we found it.
        }else{ // no dash, just a single number
            lo_cpu = hi_cpu = safe_strtoull( comma_str );
        }
        // Sanitize the input
        if( lo_cpu >= CPU_SETSIZE ){
            printf( "%s:%d:%s Requested cpu %llu exceeds the max number of cpus (%d)\n",
                    __FILE__, __LINE__, __func__, lo_cpu, CPU_SETSIZE);
            exit(-1);
        }
        if( hi_cpu >= CPU_SETSIZE ){
            printf( "%s:%d:%s Requested cpu %llu exceeds the max number of cpus (%d)\n",
                    __FILE__, __LINE__, __func__, hi_cpu, CPU_SETSIZE);
            exit(-1);
        }
        if( hi_cpu < lo_cpu ){
            printf( "%s:%d:%s cpu range %llu-%llu is reversed.\n",
                    __FILE__, __LINE__, __func__, lo_cpu, hi_cpu);
            exit(-1);
        }

        for( unsigned long long c = lo_cpu; c <= hi_cpu; c++ ){
            if ( CPU_ISSET( c, cpuset ) ){
                printf( "%s:%d:%s cpu %llu duplicated in this paramter.\n",
                        __FILE__, __LINE__, __func__, c);
                exit(1);
            }
            CPU_SET( c, cpuset );
        }
    }; // while(1)
    free( local_s );
}

unsigned int get_next_cpu( unsigned int start_cpu, unsigned int max_cpu, cpu_set_t *cpus ){
    // msr-safe currently uses a __u16 datatype to hold the cpu id.
    // This will be changing in the v2.0 release.
    // Reminder for writing non-production code:  if something unexpected happens, fall over
    // and die.
    do{
        if( CPU_ISSET( start_cpu, cpus ) ){
            return start_cpu;
        }
        start_cpu++;
    }while( start_cpu < CPU_SETSIZE && start_cpu < max_cpu );
    assert(0);
    return 0;
}

char * cpuset2str( cpu_set_t *cpus ){
    /* FIXME:  This could stand to be a bit more robust. */
    const size_t max_str_len = 1024;
    char * str = calloc( max_str_len, 1 );
    char * ptr = str;

    int count = CPU_COUNT( cpus );
    int lo_cpu = 0, hi_cpu = 0;
    while( count > 0 ){
        if( CPU_ISSET( lo_cpu, cpus ) ){
            if( lo_cpu < ( CPU_SETSIZE -2 ) ){
                hi_cpu = lo_cpu;
                // If the cpu after lo_cpu is not set, hi_cpu equals lo_cpu.
                // Otherwise, hi_cpu will be set to the last consecutive "set" cpu.
                while( ( ( hi_cpu +1 ) < CPU_SETSIZE ) && CPU_ISSET( hi_cpu+1, cpus ) ){
                    hi_cpu++;
                }
            }
            // Always print lo_cpu
            ptr += sprintf(ptr, "%d", lo_cpu);

            // Use range notation for 3 or more consecutive "set" cpus.
            if( hi_cpu - lo_cpu > 2 ){
                count -= (hi_cpu - lo_cpu) + 1;
                lo_cpu = hi_cpu+2; // We know hi_cpu+1 is unset
            }else{
                count--;
                lo_cpu++;
            }

            // Controls if there's a following comma
            ptr += sprintf( ptr, "%s", ( count > 0 ) ? ", " : "" );
        }else{
            lo_cpu++;
        }
    }
    return str;
}


void dprintf_cpuset( int fd, cpu_set_t *cpus ){
    char * str = cpuset2str( cpus );
    dprintf( fd, "%s", str );
    free( str );
}

void fprintf_cpuset( FILE *fp, cpu_set_t *cpus ){
    char * str = cpuset2str( cpus );
    fprintf( fp, "%s", str );
    free( str );
}


