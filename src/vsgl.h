/*
 * vsgl.h - interface to the PowerVR VxD (VSGL.VXD), reconstructed.
 *
 * Imagination's release leaves out the VxD and its header. PVROS
 * (win32/system.c) talks to the VxD with DeviceIoControl; this header gives
 * the control codes and the one structure it needs, and points CreateFile,
 * DeviceIoControl and CloseHandle in system.c at the wrapper's stand-in for
 * the VxD (src/vsgl_emu.c), which emulates a PCX2 board.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef VSGL_H
#define VSGL_H

#include <windows.h>

/* Control codes as in the shipped VxDs (dispatch table index - 1), from
   static analysis of the 4.1.1.5 VSGL.VXD; see Appendix F of the PCX2
   reference (github.com/z2k-retro/powervr-pcx2-tools, docs/). */
enum {
	VSGL_GET_VERSION                = 0x01,
	VSGL_ALLOCATEBLOCK              = 0x02,
	VSGL_FREEBLOCK                  = 0x03,
	VSGL_ALLOCPHYSALIAS             = 0x04,
	VSGL_FREEPHYSALIAS              = 0x05,
	VSGL_WBINVD                     = 0x08,
	VSGL_SETCACHEMODE               = 0x09,
	VSGL_COPYBUFFER                 = 0x0A,
	VSGL_GETPHYSADDRESS             = 0x0E,
	VSGL_ENUM_PHYSICAL_BOARDS       = 0x16,
	VSGL_GET_PHYSICAL_BOARD         = 0x17,
	VSGL_RELEASE_PHYSICAL_BOARD     = 0x18,
	VSGL_ENABLE_PHYSICAL_BOARD      = 0x19,
	VSGL_DISABLE_PHYSICAL_BOARD     = 0x1A,
	VSGL_GET_PHYSICAL_BOARD_INFO    = 0x1B,
	VSGL_CREATE_DMA_SCATTER_BUFFER  = 0x1C,
	VSGL_DESTROY_DMA_SCATTER_BUFFER = 0x1D,
	VSGL_ALLOC_VIRTUAL_PAGES        = 0x1E,
	VSGL_FREE_VIRTUAL_PAGES         = 0x1F,
	VSGL_PAGE_RESERVE               = 0x20,
	VSGL_PAGE_COMMIT                = 0x21,
	VSGL_PAGE_DECOMMIT              = 0x22,
	VSGL_PAGE_FREE                  = 0x23,
	VSGL_SCHEDULE_RENDER            = 0x24,
	VSGL_SCHEDULER_LOCK             = 0x25,
	VSGL_SCHEDULER_UNLOCK           = 0x26,
	VSGL_PAGE_REALLOCATE            = 0x27,
	VSGL_GET_MAN_TYPE               = 0x28
};

/* Same layout as DMABUFFER in pvrosapi.h, which system.c casts to it. */
typedef struct SHAREDBLOCK {
	unsigned long GlobalAlias;
	unsigned long Ring0Linear;
	unsigned long Pages;
	unsigned long PhysicalAddress;
} SHAREDBLOCK;

HANDLE vsgl_open(const char *name);
BOOL   vsgl_ioctl(HANDLE h, DWORD code, void *in, DWORD in_size,
                  void *out, DWORD out_size, DWORD *returned, void *overlapped);
BOOL   vsgl_close(HANDLE h);

#ifdef VSGL_REDIRECT
#undef CreateFile
#define CreateFile(name, a, s, sec, c, f, t) vsgl_open(name)
#define DeviceIoControl(h, c, i, is, o, os, r, ov) \
	vsgl_ioctl((h), (c), (void *) (i), (is), (void *) (o), (os), (DWORD *) (r), (ov))
#define CloseHandle(h) vsgl_close(h)
/* MSVC's CRT start-up hook, which system.c's DllMain called; MinGW's CRT
   is already initialised by the time DllMain runs. */
#define _CRT_INIT vsgl_crt_init
BOOL WINAPI vsgl_crt_init(HINSTANCE h, DWORD reason, LPVOID reserved);
/* system.c's DllMain is renamed so src/vsgl_emu.c can run it alongside
   SGL's own (win32/w32dll.c, renamed SGL_DllMain by the build). */
#define DllMain PVROS_DllMain
/* PVROS's messages also go to the wrapper's log file (src/diag.c). */
#undef OutputDebugString
#define OutputDebugString pvr_log_string
void WINAPI pvr_log_string(const char *s);
#endif

void pvr_log(const char *fmt, ...);

#endif
