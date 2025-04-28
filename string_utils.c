#define _GNU_SOURCE
#include <string.h>     // strtoull(3)
#include <errno.h>      // errno(3)
#include <stdio.h>      // printf(3)
#include <stdlib.h>     // exit(3)
#include <limits.h>     // ULONG_WIDTH, etc.
#include <assert.h>     // assert(3)
#include <time.h>       // struct timespec
#include "string_utils.h"

#if __LP64__            // long ints and pointers are 64 bits, ints are 32 bits.
                        // https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html

uint64_t strtouint64_t( const char *restrict nptr, char **restrict endptr, int base ){
    return strtoull( nptr, endptr, base );
}
uint32_t strtouint32_t( const char *restrict nptr, char **restrict endptr, int base ){
    return (uint32_t) strtoull( nptr, endptr, base );
}
int64_t strtoint64_t( const char *restrict nptr, char **restrict endptr, int base ){
    return strtoll( nptr, endptr, base );
}
int32_t strtoint32_t( const char *restrict nptr, char **restrict endptr, int base ){
    return (uint32_t) strtoll( nptr, endptr, base );
}
#endif //__LP64__


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

