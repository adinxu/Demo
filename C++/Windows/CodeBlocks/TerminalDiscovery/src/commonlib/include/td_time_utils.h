#ifndef TD_TIME_UTILS_H
#define TD_TIME_UTILS_H

#include <stdint.h>
#include <time.h>

static inline uint64_t timespec_diff_ms(const struct timespec *start,
                                        const struct timespec *end) {
    if (!start || !end) {
        return 0ULL;
    }

    time_t sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;

    if (sec < 0) {
        return 0ULL;
    }

    if (nsec < 0) {
        if (sec == 0) {
            return 0ULL;
        }
        sec -= 1;
        nsec += 1000000000L;
    }

    uint64_t millis = (uint64_t)sec * 1000ULL;
    millis += (uint64_t)(nsec / 1000000L);
    return millis;
}

static inline struct timespec timespec_add_ms(const struct timespec *base,
                                              uint32_t ms) {
    struct timespec result = *base;
    result.tv_sec += ms / 1000U;
    result.tv_nsec += (long)(ms % 1000U) * 1000000L;
    if (result.tv_nsec >= 1000000000L) {
        result.tv_sec += 1;
        result.tv_nsec -= 1000000000L;
    }
    return result;
}

#endif /* TD_TIME_UTILS_H */
