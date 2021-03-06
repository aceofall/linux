#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#if __LINUX_ARM_ARCH__ < 6
#error SMP not supported on pre-ARMv6 CPUs
#endif

#include <asm/processor.h>

/*
 * sev and wfe are ARMv6K extensions.  Uniprocessor ARMv6 may not have the K
 * extensions, so when running on UP, we have to patch these instructions away.
 */
#define ALT_SMP(smp, up)					\
	"9998:	" smp "\n"					\
	"	.pushsection \".alt.smp.init\", \"a\"\n"	\
	"	.long	9998b\n"				\
	"	" up "\n"					\
	"	.popsection\n"

#ifdef CONFIG_THUMB2_KERNEL
#define SEV		ALT_SMP("sev.w", "nop.w")
/*
 * For Thumb-2, special care is needed to ensure that the conditional WFE
 * instruction really does assemble to exactly 4 bytes (as required by
 * the SMP_ON_UP fixup code).   By itself "wfene" might cause the
 * assembler to insert a extra (16-bit) IT instruction, depending on the
 * presence or absence of neighbouring conditional instructions.
 *
 * To avoid this unpredictableness, an approprite IT is inserted explicitly:
 * the assembler won't change IT instructions which are explicitly present
 * in the input.
 */
#define WFE(cond)	ALT_SMP(		\
	"it " cond "\n\t"			\
	"wfe" cond ".n",			\
						\
	"nop.w"					\
)
#else
#define SEV		ALT_SMP("sev", "nop")
#define WFE(cond)	ALT_SMP("wfe" cond, "nop")
#endif

static inline void dsb_sev(void)
{
#if __LINUX_ARM_ARCH__ >= 7 // ARM10C Y 
	__asm__ __volatile__ (
		"dsb\n"
		SEV
	);  // ARM10C this 
#else
	__asm__ __volatile__ (
		"mcr p15, 0, %0, c7, c10, 4\n"
		SEV
		: : "r" (0)
	);
#endif
}

/*
 * ARMv6 ticket-based spin-locking.
 *
 * A memory barrier is required after we get a lock, and before we
 * release it, because V6 CPUs are assumed to have weakly ordered
 * memory.
 */

#define arch_spin_unlock_wait(lock) \
	do { while (arch_spin_is_locked(lock)) cpu_relax(); } while (0)

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

// ARM10C 20130831
// http://lwn.net/Articles/267968/
// http://studyfoss.egloos.com/5144295 <- 필독! spin_lock 설명
// ARM10C 20140315
// TICKET_SHIFT: 16
static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned long tmp;
	u32 newval;//다음 next 값
	arch_spinlock_t lockval;//현재 next 값
// ARM10C 20130907
// 1:	ldrex	lockval, &lock->slock
// 현재 next(lockval)는 받아 놓고,
// 	add	newval, lockval, (1<<TICKET_SHIFT) // tickets.next += 1
// 다음 next(newval) 는 += 1하고 저장 한다.
// 	strex	tmp, newval, &lock->slock
// 	teq	tmp, #0\n
// 	bne	1b
	// lock->slock에서 실제 데이터를 쓸때 (next+=1) 까지 루프
	// next+=1 의 의미는 표를 받기위해 번호표 발행
	__asm__ __volatile__(
"1:	ldrex	%0, [%3]\n"
"	add	%1, %0, %4\n"
"	strex	%2, %1, [%3]\n"
"	teq	%2, #0\n"
"	bne	1b"
	: "=&r" (lockval), "=&r" (newval), "=&r" (tmp)
	: "r" (&lock->slock), "I" (1 << TICKET_SHIFT)
	: "cc");

	// 실재 lock을 걸기 위해 busylock 한다.
	// 받은 번호표의 순을 기다린다. (unlock에서 owner을 증가 시켜서)
	while (lockval.tickets.next != lockval.tickets.owner) {
		wfe(); // 이벤트대기 (irq, frq,부정확한 중단 또는 디버그 시작 요청 대기. 구현되지 않은 경우 NOP)
		// arch_spin_unlock()의 dsb_sev()가 호출될때 깨어남
		lockval.tickets.owner = ACCESS_ONCE(lock->tickets.owner);
		// local owner값 업데이트
	}

	smp_mb();
}

// ARM10C 20130831
// lock->slock : 0
// ARM10C 20140405
static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long contended, res;
	u32 slock;


	do {
		// lock->slock이0 이면 unlocked
		// lock->slock이0x10000 이면 locked
		//
		//"	ldrex	slock, lock->slock\n"
		//"	subs	tmp,   slock, slock, ror #16\n"
		//위 코드의 의미
		//if( next == owner )//현재 락을 가져도 된다.
		//"	addeq	slock, slock, (1 << TICKET_SHIFT)\n"
		//"	strexeq	tmp,   slock, lock->slock"
		__asm__ __volatile__(
		"	ldrex	%0, [%3]\n"
		"	mov	%2, #0\n"
		"	subs	%1, %0, %0, ror #16\n"
		"	addeq	%0, %0, %4\n"
		"	strexeq	%2, %0, [%3]"
		: "=&r" (slock), "=&r" (contended), "=&r" (res)
		: "r" (&lock->slock), "I" (1 << TICKET_SHIFT)
		: "cc");
	} while (res);

	if (!contended) {
		smp_mb();   // ARM10C dmb()
		return 1;
	} else {
		return 0;
	}
}

// ARM10C 20130322
// ARM10C 20140412
static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	smp_mb(); // smb_mb(), dsb_sev() 중 dsb()는 owner를 보호하기위한 것
	lock->tickets.owner++;
	dsb_sev(); // 이벤트발생
}

// ARM10C 20140315
// arch_spin_is_locked(&(&(&(&cpu_add_remove_lock)->wait_lock)->rlock)->raw_lock)
static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	// lock->tickets: (&(&(&(&cpu_add_remove_lock)->wait_lock)->rlock)->raw_lock)->tickets
        // ACCESS_ONCE((&(&(&(&cpu_add_remove_lock)->wait_lock)->rlock)->raw_lock)->tickets):
	// (*(volatile struct __raw_tickets *)&((&(&(&(&cpu_add_remove_lock)->wait_lock)->rlock)->raw_lock)->tickets))
	struct __raw_tickets tickets = ACCESS_ONCE(lock->tickets);

	// tickets.owner: 0, tickets.next: 1
	return tickets.owner != tickets.next;
	// return 1
}

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	struct __raw_tickets tickets = ACCESS_ONCE(lock->tickets);
	return (tickets.next - tickets.owner) > 1;
}
#define arch_spin_is_contended	arch_spin_is_contended

/*
 * RWLOCKS
 *
 *
 * Write locks are easy - we just set bit 31.  When unlocking, we can
 * just write zero since the lock is exclusively held.
 */

// ARM10C 20140125
// ARM10C 20140405
static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
"1:	ldrex	%0, [%1]\n"
"	teq	%0, #0\n"
	WFE("ne")
"	strexeq	%0, %2, [%1]\n"
"	teq	%0, #0\n"
"	bne	1b"
	: "=&r" (tmp)
	: "r" (&rw->lock), "r" (0x80000000)
	: "cc");

	smp_mb();
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned long contended, res;

	do {
		__asm__ __volatile__(
		"	ldrex	%0, [%2]\n"
		"	mov	%1, #0\n"
		"	teq	%0, #0\n"
		"	strexeq	%1, %3, [%2]"
		: "=&r" (contended), "=&r" (res)
		: "r" (&rw->lock), "r" (0x80000000)
		: "cc");
	} while (res);

	if (!contended) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

// ARM10C 20140125
static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	smp_mb();

	__asm__ __volatile__(
	"str	%1, [%0]\n"
	:
	: "r" (&rw->lock), "r" (0)
	: "cc");

	dsb_sev();
}

/* write_can_lock - would write_trylock() succeed? */
#define arch_write_can_lock(x)		((x)->lock == 0)

/*
 * Read locks are a bit more hairy:
 *  - Exclusively load the lock value.
 *  - Increment it.
 *  - Store new lock value if positive, and we still own this location.
 *    If the value is negative, we've already failed.
 *  - If we failed to store the value, we want a negative result.
 *  - If we failed, try again.
 * Unlocking is similarly hairy.  We may have multiple read locks
 * currently active.  However, we know we won't have any write
 * locks.
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long tmp, tmp2;

	__asm__ __volatile__(
"1:	ldrex	%0, [%2]\n"
"	adds	%0, %0, #1\n"
"	strexpl	%1, %0, [%2]\n"
	WFE("mi")
"	rsbpls	%0, %1, #0\n"
"	bmi	1b"
	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&rw->lock)
	: "cc");

	smp_mb();
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp, tmp2;

	smp_mb();

	__asm__ __volatile__(
"1:	ldrex	%0, [%2]\n"
"	sub	%0, %0, #1\n"
"	strex	%1, %0, [%2]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&rw->lock)
	: "cc");

	if (tmp == 0)
		dsb_sev();
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned long contended, res;

	do {
		__asm__ __volatile__(
		"	ldrex	%0, [%2]\n"
		"	mov	%1, #0\n"
		"	adds	%0, %0, #1\n"
		"	strexpl	%1, %0, [%2]"
		: "=&r" (contended), "=&r" (res)
		: "r" (&rw->lock)
		: "cc");
	} while (res);

	/* If the lock is negative, then it is already held for write. */
	if (contended < 0x80000000) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

/* read_can_lock - would read_trylock() succeed? */
#define arch_read_can_lock(x)		((x)->lock < 0x80000000)

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
