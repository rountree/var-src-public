#define _GNU_SOURCE
#include <string.h>     // strtoull(3)
#include <errno.h>      // errno(3)
#include <stdio.h>      // printf(3)
#include <stdlib.h>     // exit(3)
#include <assert.h>     // assert(3)
#include <time.h>       // struct timespec
#include <stdint.h>
#include <inttypes.h>
#include "timespec_utils.h"

char* timespec2str( const struct timespec * const t ){


    const uint64_t ns_per_us= (uint64_t)1'000;
    const uint64_t ns_per_ms= ns_per_us * 1'000;
    const uint64_t ns_per_s = ns_per_ms * 1'000;
    const uint64_t ns_per_m = ns_per_s * 60;
    const uint64_t ns_per_h = ns_per_m * 60;
    const uint64_t ns_per_d = ns_per_h * 24;

    const uint64_t ns       = t->tv_sec * ns_per_s + t->tv_nsec;

    char *str = calloc( 1024, 1 );


    // Largest unit is days
    if( ns >= ns_per_d){
        if( ns % ns_per_d ){
            sprintf( str, "%"PRIu64".%03"PRIu64"d",
                    ns / ns_per_d,
                    (ns % ns_per_d) / ( ns_per_d / 1000 ) );
        }else{
            sprintf( str, "%"PRIu64"d", ns / ns_per_d );
        }
    // Largest unit is hours
    }else if( ns >= ns_per_h){
        if( ns % ns_per_h ){
            sprintf( str, "%"PRIu64".%03"PRIu64"h",
                    ns / ns_per_h,
                    (ns % ns_per_h) / ( ns_per_h / 1000 ) );
        }else{
            sprintf( str, "%"PRIu64"h", ns / ns_per_h );
        }
    // Largest unit is minutes
    }else if( ns >= ns_per_m){
        if( ns % ns_per_m ){
            sprintf( str, "%"PRIu64".%03"PRIu64"m",
                    ns / ns_per_m,
                    (ns % ns_per_m) / ( ns_per_m / 1000 ) );
        }else{
            sprintf( str, "%"PRIu64"m", ns / ns_per_m );
        }
    // Largest unit is seconds
    }else if( ns >= ns_per_s){
        if( ns % ns_per_s ){
            sprintf( str, "%"PRIu64".%03"PRIu64"s",
                    ns / ns_per_s,
                    (ns % ns_per_s) / ( ns_per_s / 1000 ) );
        }else{
            sprintf( str, "%"PRIu64"s", ns / ns_per_s );
        }
    // Largest unit is milliseconds
    }else if( ns >= ns_per_ms){
        if( ns % ns_per_ms ){
            sprintf( str, "%"PRIu64".%03"PRIu64"ms",
                    ns / ns_per_ms,
                    ( ns % ns_per_ms ) / ( ns_per_ms / 1000 ) );
        }else{
            sprintf( str, "%"PRIu64"ms", ns / ns_per_ms );
        }
    // Largest unit is microseconds
    }else if( ns >= ns_per_us){
        if( ns % ns_per_us ){
            sprintf( str, "%"PRIu64".%03"PRIu64"us",
                    ns / ns_per_us,
                    ( ns % ns_per_us ) / ( ns_per_us / 1000 ) );;
        }else{
            sprintf( str, "%"PRIu64"us", ns / ns_per_us );
        }
    // Largest unit is naonoseconds
    }else{
        sprintf( str, "%"PRIu64"ns", ns );
    }
    return str;
}

void str2timespec( const char * const restrict s, struct timespec *t ){

    char *endptr;
    time_t val;

    assert( sizeof( time_t ) == sizeof( unsigned long long ) );

    errno = 0;
    val = strtoull( s, &endptr, 0 );
    if( ERANGE == errno ){
        printf("%s:%d:%s String passed to str2timespec results in an out-of-range value (%s).  Bye!\n",
                __FILE__, __LINE__, __func__, s);
        exit(-1);
    }

    if( endptr == s ){
        // Handle null string / non-numeric string case
        printf( "%s:%d:%s String with no numeric data passed to str2timespec (%s).  Bye!\n",
                __FILE__, __LINE__, __func__, s );
        exit(-1);
    }else if( '\0' == *endptr ){
        // No time units provided, assume seconds.
        t->tv_sec  = val;
        t->tv_nsec = 0;
    }else{// parse the units.
        if( 0 == strncmp( endptr, "ns", strlen("ns") ) ){
            // nanoseconds (ns)
            t->tv_sec  = val / 1'000'000'000ULL;
            t->tv_nsec = val % 1'000'000'000ULL;
        }else if( 0 == strncmp( endptr, "us", strlen("us") ) ){
            // microseconds (us)
            t->tv_sec  = val / 1'000'000ULL;
            t->tv_nsec = (val % 1'000'000ULL) * 1'000ULL;
        }else if( 0 == strncmp( endptr, "ms", strlen("ms") ) ){
            // milliseconds (ms)
            t->tv_sec  = val / 1'000ULL;
            t->tv_nsec = (val % 1'000ULL) * 1'000'000ULL;
        }else if( 0 == strncmp( endptr, "s", strlen("s") ) ){
            // seconds (s)
            t->tv_sec  = val;
            t->tv_nsec = 0;
        }else if( 0 == strncmp( endptr, "m", strlen("m") ) ){
            // minutes (m), must check after "ms"
            t->tv_sec  = val * 60;
            t->tv_nsec = 0;
        }else if( 0 == strncmp( endptr, "h", strlen("h") ) ){
            // hours (h)
            t->tv_sec  = val * 60 * 60;
            t->tv_nsec = 0;
        }else if( 0 == strncmp( endptr, "d", strlen("d") ) ){
            // days (d)
            t->tv_sec  = val * 60 * 60 * 24;
            t->tv_nsec = 0;
        }else{
            printf( "%s:%d:%s String with unknown units passed to str2timespec (%s).  Bye!\n",
                    __FILE__, __LINE__, __func__, s );
            exit(-1);
        }
        return;
    }
}

size_t timespec_division( const struct timespec * const restrict numerator, const struct timespec * const restrict denominator ){
    // Overflow shouldn't be an issue so long as sizeof(size_t) == 8.
    size_t n = numerator->tv_sec   * 1'000'000'000ULL + numerator->tv_nsec;
    size_t d = denominator->tv_sec * 1'000'000'000ULL + denominator->tv_nsec;
    return n/d;
}

void fprintf_timespec( FILE * const restrict fp, const struct timespec * const restrict t ){
    char *s = timespec2str( t );
    fprintf( fp, "%s", s );
    free(s);
}
