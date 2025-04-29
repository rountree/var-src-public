#define _GNU_SOURCE
#include <string.h>     // strtoull(3)
#include <errno.h>      // errno(3)
#include <stdio.h>      // printf(3)
#include <stdlib.h>     // exit(3)
#include <limits.h>     // ULONG_WIDTH, etc.
#include <assert.h>     // assert(3)
#include <time.h>       // struct timespec
#include "string_utils.h"

// Recall that a [un]signed long long is guaranteed to be at least 64 bits, and
// a [un]signed long will either be at least 64 bits (LP64) or at least 32 bits.
// If we want a 32-bit value, use strto[u]ll, check the range, and make the cast.

uint64_t strtouint64_t( const char *restrict nptr, char **restrict endptr, int base ){
    return strtoull( nptr, endptr, base );
}
uint32_t strtouint32_t( const char *restrict nptr, char **restrict endptr, int base ){
    uint64_t value = strtoull( nptr, endptr, base );
    assert( value <= (( 1ULL << 32 ) - 1) );
    return (uint32_t) value;
}


unsigned long long safe_strtoull( const char * restrict s ){
    // Assume <s> is a string containing only numerically relevant characters
    // and base is 0 (can handle octal, hex, and decimal).
    // Exit with error message if other characters found or overflow.

    char *endptr;
    errno = 0;
    unsigned long long x = strtoull( s, &endptr, 0 );
    if( ERANGE == errno ){
        printf( "%s:%d:%s Converting <%s> to an unsigned long long would overflow.\n",
                __FILE__, __LINE__, __func__, s);
        exit(-1);
    }
    else if( '\0' != *endptr ){
        printf( "%s:%d:%s Unexpected character '%c' (%#x) in numeric string <%s>.\n",
                __FILE__, __LINE__, __func__, *endptr, *endptr, s);
        exit(-1);
    }else if( s == endptr ){
        printf( "%s:%d:%s Request to convert empty string to unsigned long long.\n",
                __FILE__, __LINE__, __func__);
        exit(-1);
    };
    return x;
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
        }else if( 0 == strncmp( endptr, "d", strlen("h") ) ){
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

// Yeah, this doesn't really belong in string_utils...
size_t timespec_division( const struct timespec * const restrict numerator, const struct timespec * const restrict denominator ){
    // Overflow shouldn't be an issue so long as sizeof(size_t) == 8.
    size_t n = numerator->tv_sec   * 1'000'000'000ULL + numerator->tv_nsec;
    size_t d = denominator->tv_sec * 1'000'000'000ULL + denominator->tv_nsec;
    return n/d;
}
