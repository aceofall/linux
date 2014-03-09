#ifndef __LINUX_SPINLOCK_TYPES_H
#define __LINUX_SPINLOCK_TYPES_H

/*
 * include/linux/spinlock_types.h - generic spinlock type definitions
 *                                  and initializers
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */

#if defined(CONFIG_SMP) // CONFIG_SMP=y
# include <asm/spinlock_types.h>
#else
# include <linux/spinlock_types_up.h>
#endif

#include <linux/lockdep.h>

// ARM10C 20130914
// arch_spinlock_t의 wrapper다.
typedef struct raw_spinlock {
	arch_spinlock_t raw_lock;
//CONFIG_GENERIC_LOCKBREAK = n
#ifdef CONFIG_GENERIC_LOCKBREAK
	unsigned int break_lock;
#endif
//CONFIG_DEBUG_SPINLOCK = y
#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned int magic, owner_cpu;
	void *owner;
#endif
//CONFIG_DEBUG_LOCK_ALLOC = n
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} raw_spinlock_t;

// KID 20140114
#define SPINLOCK_MAGIC		0xdead4ead

// KID 20140114
#define SPINLOCK_OWNER_INIT	((void *)-1L)

#ifdef CONFIG_DEBUG_LOCK_ALLOC // CONFIG_DEBUG_LOCK_ALLOC=n
# define SPIN_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname }
#else
// KID 20140114
// SPIN_DEP_MAP_INIT(printk_ratelimit_state.lock) 
# define SPIN_DEP_MAP_INIT(lockname)
#endif

#ifdef CONFIG_DEBUG_SPINLOCK // CONFIG_DEBUG_SPINLOCK=y
// KID 20140114
// SPINLOCK_MAGIC: 0xdead4ead
// SPINLOCK_OWNER_INIT:	((void *)-1L)
// SPIN_DEBUG_INIT(printk_ratelimit_state.lock)	
// # define SPIN_DEBUG_INIT(printk_ratelimit_state.lock)
// 	.magic = 0xdead4ead,
// 	.owner_cpu = -1,
// 	.owner = ((void *)-1L),
# define SPIN_DEBUG_INIT(lockname)		\
	.magic = SPINLOCK_MAGIC,		\
	.owner_cpu = -1,			\
	.owner = SPINLOCK_OWNER_INIT,
#else
# define SPIN_DEBUG_INIT(lockname)
#endif

// ARM10C 20130914
// KID 20140114
// __RAW_SPIN_LOCK_INITIALIZER(printk_ratelimit_state.lock)
// __ARCH_SPIN_LOCK_UNLOCKED:	{ { 0 } }
// #define SPIN_DEBUG_INIT(printk_ratelimit_state.lock)
// 	.magic = 0xdead4ead,
// 	.owner_cpu = -1,
// 	.owner = ((void *)-1L),
// #define SSPIN_DEP_MAP_INIT(printk_ratelimit_state.lock) 
// #define __RAW_SPIN_LOCK_INITIALIZER(printk_ratelimit_state.lock)
// 	{
// 	  .raw_lock = { { 0 } },
// 	  .magic = 0xdead4ead,
// 	  .owner_cpu = -1,
// 	  .owner = ((void *)-1L),
// 	}
#define __RAW_SPIN_LOCK_INITIALIZER(lockname)	\
	{					\
	.raw_lock = __ARCH_SPIN_LOCK_UNLOCKED,	\
	SPIN_DEBUG_INIT(lockname)		\
	SPIN_DEP_MAP_INIT(lockname) }

// KID 20140114
// __RAW_SPIN_LOCK_UNLOCKED(printk_ratelimit_state.lock),
// __RAW_SPIN_LOCK_INITIALIZER(printk_ratelimit_state.lock)
// #define __RAW_SPIN_LOCK_INITIALIZER(printk_ratelimit_state.lock)
// 	{
// 	  .raw_lock = { { 0 } },
// 	  .magic = 0xdead4ead,
// 	  .owner_cpu = -1,
// 	  .owner = ((void *)-1L),
// 	}
#define __RAW_SPIN_LOCK_UNLOCKED(lockname)	\
	(raw_spinlock_t) __RAW_SPIN_LOCK_INITIALIZER(lockname)

#define DEFINE_RAW_SPINLOCK(x)	raw_spinlock_t x = __RAW_SPIN_LOCK_UNLOCKED(x)

// ARM10C 20130914
// 여기도 raw_spinlock의 wrapper다.
typedef struct spinlock {
	union {
		struct raw_spinlock rlock;
// CONFIG_DEBUG_LOCK_ALLOC = n
#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define LOCK_PADSIZE (offsetof(struct raw_spinlock, dep_map))
		struct {
			u8 __padding[LOCK_PADSIZE];
			struct lockdep_map dep_map;
		};
#endif
	};
} spinlock_t;

// ARM10C 20131012
#define __SPIN_LOCK_INITIALIZER(lockname) \
	{ { .rlock = __RAW_SPIN_LOCK_INITIALIZER(lockname) } }

// ARM10C 20131012
#define __SPIN_LOCK_UNLOCKED(lockname) \
	(spinlock_t ) __SPIN_LOCK_INITIALIZER(lockname)

#define DEFINE_SPINLOCK(x)	spinlock_t x = __SPIN_LOCK_UNLOCKED(x)

#include <linux/rwlock_types.h>

#endif /* __LINUX_SPINLOCK_TYPES_H */
