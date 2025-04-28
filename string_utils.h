#pragma once

unsigned long long safe_strtoull( const char * restrict s );
void str2timespec( const char * const restrict s, struct timespec *t );
