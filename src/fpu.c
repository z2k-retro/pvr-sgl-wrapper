/*
 * fpu.c - C replacements for SetupFPU/RestoreFPU from win32/fast.asm.
 *
 * The original driver switched the x87 to single precision with truncating
 * rounding (control word bits 8-9 = 00, bits 10-11 = 11) around its geometry
 * code. The wrapper does the same so its arithmetic matches the original's.
 *
 * SPDX-License-Identifier: MIT
 */

static unsigned short wFPUFlags;
static unsigned short wFPUSaveFlags;

void __cdecl SetupFPU(void)
{
	__asm__ __volatile__("fnstcw %0" : "=m"(wFPUSaveFlags));
	wFPUFlags = (unsigned short) ((wFPUSaveFlags & 0xFCFF) | 0x0C00);
	__asm__ __volatile__("fldcw %0" : : "m"(wFPUFlags));
}

void __cdecl RestoreFPU(void)
{
	__asm__ __volatile__("fldcw %0" : : "m"(wFPUSaveFlags));
}
