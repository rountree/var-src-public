#pragma once
#include <stdint.h>
#if __LP64__
uint64_t strtouint64_t( const char *restrict nptr, char **restrict endptr, int base );
uint32_t strtouint32_t( const char *restrict nptr, char **restrict endptr, int base );
int64_t strtoint64_t( const char *restrict nptr, char **restrict endptr, int base );
int32_t strtoint32_t( const char *restrict nptr, char **restrict endptr, int base );
#endif // __LP64__

unsigned long long safe_strtoull( const char * restrict s );
void str2timespec( const char * const restrict s, struct timespec *t );
