#pragma once
#include <stdio.h>
#include <stdint.h>
void str2timespec( const char * const restrict s, struct timespec *t );
char* timespec2str( const struct timespec * const t );
size_t timespec_division( const struct timespec * const restrict numerator, const struct timespec * const restrict denominator );
void dprintf_timespec( int fd, const struct timespec * const restrict t );
void fprintf_timespec( FILE * const restrict fp, const struct timespec * const restrict t );

