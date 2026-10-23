/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box (João Viana): the sampling profiler's TIMER5 entry (Felucca's FM1_ISR shape, fm1_isr.S), passing the
 * interrupted PC (reti) to the C body. */
	.section .text.isr_jbprof,"ax",@progbits
	.globl isr_jbprof
isr_jbprof:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	r0 = reti
	call jb_prof_irq
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
