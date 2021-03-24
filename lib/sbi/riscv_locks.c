/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 * Copyright (c) 2021 Christoph Müllner <cmuellner@linux.com>
 */

#include <sbi/riscv_barrier.h>
#include <sbi/riscv_locks.h>

static inline int spin_lock_unlocked(spinlock_t lock)
{
        return lock.owner == lock.next;
}

bool spin_lock_check(spinlock_t *lock)
{
	RISCV_FENCE(r, rw);
	return !spin_lock_unlocked(*lock);
}

int spin_trylock(spinlock_t *lock)
{
	u32 tmp0, tmp1, tmp2;
	u32 inc = 1u << (offsetof(spinlock_t, next));

	__asm__ __volatile__(
		"1:	li	%1, -65536\n"	//tmp1 = 0xffff0000
		"	lr.w.aq	%0, %3\n"	//tmp0 = *lock
		"	slliw	%2, %0, 16\n"	//tmp2 = tmp0 << 16
		"	and	%1, %0, %1\n"	//tmp1 = tmp0 & 0xffff0000
		"	bne	%1, %2, 2f\n"	//return if tmp1 != tmp2 (already locked)
		"	addw	%0, %0, %4\n"	//tmp0 = tmp0 + inc
		"	sc.w.rl	%0, %0, %3\n"	//*lock = tmp0
		"	bnez	%0, 1b\n"	//tmp0 == 0
		"2:"
		: "=&r"(tmp0), "=&r"(tmp1), "=&r"(tmp2), "+A"(*lock)
		: "I"(inc)
		: "memory");

	return !tmp0;
}

void spin_lock(spinlock_t *lock)
{
	u32 tmp0, tmp1, tmp2;
	u32 inc = 1u << (offsetof(spinlock_t, next));

	__asm__ __volatile__(
		/* Atomically increment the next ticket. */
		"0:	lr.w.aq	%0, %3\n"	//tmp0 = *lock
		"	addw	%0, %0, %4\n"	//tmp0 = tmp0 + inc
		"	sc.w.rl	%0, %0, %3\n"	//*lock = tmp0
		"	bnez	%0, 1b\n"	//tmp0 == 0

		/* Did we get the lock? */
		"1:	li	%1, -65536\n"	//tmp1 = 0xffff0000
		"	slliw	%2, %0, 16\n"	//tmp2 = tmp0 << 16
		"	and	%1, %0, %1\n"	//tmp1 = tmp0 & 0xffff0000
		"	beq	%1, %2, 3f\n"	//return if tmp1 == tmp2 (we own the lock)
		/* No, let's spin on the lock. */
		"	lr.w.aq	%0, %3\n"	//tmp0 = *lock
		"	j	1b\n"
		"3:"
		: "=&r"(tmp0), "=&r"(tmp1), "=&r"(tmp2), "+A"(*lock)
		: "I"(inc)
		: "memory");
}

void spin_unlock(spinlock_t *lock)
{
	u32 inc = 1u << (offsetof(spinlock_t, owner) * sizeof(u8));

	__asm__ __volatile__(
		"	amoadd.w %0, %0, %1\n"
		RISCV_RELEASE_BARRIER
		: "=r"(inc), "+A"(*lock)
		:
		: "memory");
}

