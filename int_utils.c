#define _GNU_SOURCE
#include <string.h>     // strtoull(3)
#include <errno.h>      // errno(3)
#include <stdio.h>      // printf(3)
#include <stdlib.h>     // exit(3)
#include <limits.h>     // ULONG_WIDTH, etc.
#include <assert.h>     // assert(3)
#include <time.h>       // struct timespec
#include "int_utils.h"

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

