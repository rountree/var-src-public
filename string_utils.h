#pragma once
#include <stdint.h>
uint64_t strtouint64_t( const char *restrict nptr, char **restrict endptr, int base );
uint32_t strtouint32_t( const char *restrict nptr, char **restrict endptr, int base );

unsigned long long safe_strtoull( const char * restrict s );
void str2timespec( const char * const restrict s, struct timespec *t );
size_t timespec_division( const struct timespec * const restrict numerator, const struct timespec * const restrict denominator );
