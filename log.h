#ifndef CURO_LOG_H
#define CURO_LOG_H

#include "config.h"

#if DEBUG_ETHERNET > 0
#define LOG_ETHERNET(...) printf("[ether] ");printf(__VA_ARGS__)
#else
#define LOG_ETHERNET(...)
#endif

#if DEBUG_IPV6 > 0
#define LOG_IPV6(...) printf("[ipv6] ");printf(__VA_ARGS__);
#else
#define LOG_IPV6(...)
#endif

#if DEBUG_ICMPV6 > 0
#define LOG_ICMPV6(...)                                                                                                                                                                                  \
    printf("[icmpv6] ");                                                                                                                                                                                 \
    printf(__VA_ARGS__);
#else
#define LOG_ICMPV6(...)
#endif

#define LOG_INFO(...)  printf("[info] "); printf(__VA_ARGS__)
#define LOG_ERROR(...) fprintf(stderr, "[error %s:%d] ", __FILE__, __LINE__);fprintf(stderr, __VA_ARGS__);

#endif //CURO_LOG_H
