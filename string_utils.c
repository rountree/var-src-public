#define _GNU_SOURCE
#include <string.h>     // strtoull(3)
#include <errno.h>      // errno(3)
#include <stdio.h>      // printf(3)
#include <stdlib.h>     // exit(3)

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
