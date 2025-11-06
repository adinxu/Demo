#ifndef TD_ATOMIC_H
#define TD_ATOMIC_H

#include <stdbool.h>

#if defined(TD_ATOMIC_FORCE_FALLBACK)
#  define TD_ATOMIC_HAS_STDATOMIC 0
#elif defined(__GNUC__) && (__GNUC__ < 5)
#  define TD_ATOMIC_HAS_STDATOMIC 0
#endif

#if !defined(TD_ATOMIC_HAS_STDATOMIC)
#  if defined(__has_include)
#    if __has_include(<stdatomic.h>)
#      include <stdatomic.h>
#      define TD_ATOMIC_HAS_STDATOMIC 1
#    endif
#  endif
#endif

#if !defined(TD_ATOMIC_HAS_STDATOMIC)
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#    include <stdatomic.h>
#    define TD_ATOMIC_HAS_STDATOMIC 1
#  endif
#endif

#if !defined(TD_ATOMIC_HAS_STDATOMIC)
#  define TD_ATOMIC_HAS_STDATOMIC 0
#endif

#if !TD_ATOMIC_HAS_STDATOMIC

typedef volatile bool atomic_bool;

#if defined(__has_builtin)
#  if __has_builtin(__sync_synchronize)
#    define TD_ATOMIC_BARRIER() __sync_synchronize()
#  else
#    define TD_ATOMIC_BARRIER() ((void)0)
#  endif
#else
#  if defined(__GNUC__)
#    define TD_ATOMIC_BARRIER() __sync_synchronize()
#  else
#    define TD_ATOMIC_BARRIER() ((void)0)
#  endif
#endif

static inline void atomic_init(atomic_bool *obj, bool value) {
    if (obj) {
        *obj = value;
    }
}

static inline void atomic_store(atomic_bool *obj, bool value) {
    TD_ATOMIC_BARRIER();
    *obj = value;
    TD_ATOMIC_BARRIER();
}

static inline bool atomic_load(const atomic_bool *obj) {
    TD_ATOMIC_BARRIER();
    bool value = obj ? *obj : false;
    TD_ATOMIC_BARRIER();
    return value;
}

#undef TD_ATOMIC_BARRIER

#endif /* !TD_ATOMIC_HAS_STDATOMIC */

#endif /* TD_ATOMIC_H */
