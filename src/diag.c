/*
 * diag.c - a log file for diagnosing games, pvrsgl.log next to sgl.dll.
 *
 * It records the driver's own messages (PVROSPrintf and the wrapper's) and,
 * through a vectored exception handler, any crash: where it happened (module
 * and offset), the registers, and the return addresses on the stack that
 * fall inside a loaded module, so a crash inside a game's renderer can be
 * told from one inside SGL. Set PVR_LOG=0 to turn it off.
 *
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

static HANDLE log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_lock;
static int log_ready, crashes_logged;
static char dll_dir[MAX_PATH];

static LARGE_INTEGER t0, tfreq;
static int at_line_start = 1;

/* Seconds since sgl.dll was loaded. */
double pvr_log_time(void)
{
	LARGE_INTEGER t;
	if (!tfreq.QuadPart)
		return 0.0;
	QueryPerformanceCounter(&t);
	return (double) (t.QuadPart - t0.QuadPart) / (double) tfreq.QuadPart;
}

/* Each line after the header starts with the time since sgl.dll loaded. */
static void log_raw(const char *s)
{
	DWORD n;
	char stamp[24];
	if (!log_ready)
		return;
	EnterCriticalSection(&log_lock);
	if (log_file != INVALID_HANDLE_VALUE) {
		while (*s) {
			const char *nl = strchr(s, '\n');
			size_t len = nl ? (size_t) (nl - s + 1) : strlen(s);
			if (at_line_start && tfreq.QuadPart) {
				_snprintf(stamp, sizeof stamp, "[%7.3f] ", pvr_log_time());
				WriteFile(log_file, stamp, (DWORD) strlen(stamp), &n, NULL);
			}
			WriteFile(log_file, s, (DWORD) len, &n, NULL);
			at_line_start = nl != NULL;
			s += len;
		}
	}
	LeaveCriticalSection(&log_lock);
}

void pvr_log(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	OutputDebugStringA(buf);
	log_raw(buf);
}

/* For system.c's OutputDebugString (see vsgl.h). */
void WINAPI pvr_log_string(const char *s)
{
	OutputDebugStringA(s);
	log_raw(s);
}

/* "module+offset" for an address, if it is inside a loaded module. */
static int describe(DWORD addr, char *out, int size)
{
	HMODULE m;
	char path[MAX_PATH], *name;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        (LPCSTR) (ULONG_PTR) addr, &m))
		return 0;
	if (!GetModuleFileNameA(m, path, sizeof path))
		return 0;
	name = strrchr(path, '\\');
	name = name ? name + 1 : path;
	_snprintf(out, size, "%s+0x%lx", name, addr - (DWORD) (ULONG_PTR) m);
	return 1;
}

static LONG CALLBACK on_exception(PEXCEPTION_POINTERS ep)
{
	EXCEPTION_RECORD *er = ep->ExceptionRecord;
	CONTEXT *c = ep->ContextRecord;
	DWORD code = er->ExceptionCode, *sp;
	char where[MAX_PATH + 32] = "?", ret[MAX_PATH + 32];
	int i, found = 0;

	/* Only faults that end games; skip breakpoints, C++ throws, etc. */
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
	    code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION &&
	    code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_FLT_INVALID_OPERATION &&
	    code != EXCEPTION_FLT_DIVIDE_BY_ZERO)
		return EXCEPTION_CONTINUE_SEARCH;
	if (crashes_logged++ >= 8)
		return EXCEPTION_CONTINUE_SEARCH;

	describe((DWORD) (ULONG_PTR) er->ExceptionAddress, where, sizeof where);
	pvr_log("\n*** exception 0x%08lx at %p (%s)\n", code, er->ExceptionAddress, where);
	if (code == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
		pvr_log("    %s address 0x%08lx\n",
		        er->ExceptionInformation[0] == 0 ? "reading" :
		        er->ExceptionInformation[0] == 1 ? "writing" : "executing",
		        (unsigned long) er->ExceptionInformation[1]);
	pvr_log("    eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n",
	        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi);
	pvr_log("    ebp=%08lx esp=%08lx eip=%08lx fpucw=%04lx\n",
	        c->Ebp, c->Esp, c->Eip, c->FloatSave.ControlWord & 0xffff);

	/* Return addresses: stack words that point into a module's code. */
	sp = (DWORD *) (ULONG_PTR) c->Esp;
	pvr_log("    stack:");
	for (i = 0; i < 512 && found < 24; i++) {
		DWORD v;
		if (IsBadReadPtr(sp + i, 4))
			break;
		v = sp[i];
		if (v > 0x10000 && describe(v, ret, sizeof ret) && !strstr(ret, "+0x0 ")) {
			MEMORY_BASIC_INFORMATION mbi;
			if (VirtualQuery((void *) (ULONG_PTR) v, &mbi, sizeof mbi) &&
			    (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
				pvr_log(" %s", ret);
				found++;
			}
		}
	}
	pvr_log("\n");
	return EXCEPTION_CONTINUE_SEARCH;
}

/* The folder sgl.dll is in, with a trailing backslash ("" if unknown). */
const char *pvr_dll_dir(void)
{
	return dll_dir;
}

/* ---------------------------------------------------------------------
 * Start-up profile. For the first PVR_PROFILE seconds (default 20; 0
 * turns it off) the thread that loaded sgl.dll is sampled every 5 ms.
 * The log then lists where it spent its time, overall and second by
 * second, so a slow start can be traced to a module and offset. Inside
 * Windows' own DLLs the first return address in a non-system module is
 * shown as well (who was waiting).
 * ------------------------------------------------------------------- */
#define PROF_SLOTS 2048
static struct prof_slot { DWORD eip, caller; unsigned hits; } prof[PROF_SLOTS];
static HANDLE prof_thread_handle;
static unsigned prof_seconds = 20;

static int is_system_module(DWORD addr)
{
	char where[MAX_PATH + 32];
	if (!describe(addr, where, sizeof where))
		return 1; /* not in any module: treat as system */
	return !_strnicmp(where, "ntdll", 5) || !_strnicmp(where, "kernelbase", 10) ||
	       !_strnicmp(where, "kernel32", 8) || !_strnicmp(where, "win32u", 6) ||
	       !_strnicmp(where, "user32", 6) || !_strnicmp(where, "gdi32", 5);
}

/* The first return address on the copied stack that lies outside
   Windows' own DLLs. */
static DWORD find_caller(const DWORD *stack, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		HMODULE m;
		if (stack[i] <= 0x10000 || is_system_module(stack[i]))
			continue;
		/* Skip values that merely point at a module's headers. */
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                       (LPCSTR) (ULONG_PTR) stack[i], &m) &&
		    stack[i] - (DWORD) (ULONG_PTR) m >= 0x1000)
			return stack[i];
	}
	return 0;
}

static void prof_add(struct prof_slot *tab, int size, DWORD eip, DWORD caller)
{
	unsigned h = (eip * 2654435761u ^ caller) % (unsigned) size, i;
	for (i = 0; i < (unsigned) size; i++, h = (h + 1) % (unsigned) size) {
		if (!tab[h].hits) {
			tab[h].eip = eip;
			tab[h].caller = caller;
		}
		if (tab[h].eip == eip && tab[h].caller == caller) {
			tab[h].hits++;
			return;
		}
	}
}

static void prof_print(struct prof_slot *tab, int size, unsigned total, int top)
{
	int k;
	for (k = 0; k < top; k++) {
		int i, best = -1;
		char a[MAX_PATH + 32], b[MAX_PATH + 32];
		for (i = 0; i < size; i++)
			if (tab[i].hits && (best < 0 || tab[i].hits > tab[best].hits))
				best = i;
		if (best < 0)
			break;
		if (!describe(tab[best].eip, a, sizeof a))
			_snprintf(a, sizeof a, "%08lx", tab[best].eip);
		if (!tab[best].caller || !describe(tab[best].caller, b, sizeof b))
			b[0] = 0;
		pvr_log("    %5.1f%%  %s%s%s%s\n", 100.0 * tab[best].hits / total, a,
		        b[0] ? "  (waiting for " : "", b, b[0] ? ")" : "");
		tab[best].hits = 0;
	}
}

static DWORD WINAPI prof_thread(LPVOID unused)
{
	static struct prof_slot sec[256];
	unsigned total = 0, sec_total = 0, second = 0;
	double start = pvr_log_time();
	(void) unused;

	pvr_log("profile: sampling the loading thread for %u s\n", prof_seconds);
	for (;;) {
		CONTEXT c;
		double now = pvr_log_time();
		if ((unsigned) (now - start) != second || now - start >= prof_seconds) {
			if (sec_total) {
				pvr_log("profile: second %u\n", second);
				prof_print(sec, 256, sec_total, 3);
			}
			memset(sec, 0, sizeof sec);
			sec_total = 0;
			second = (unsigned) (now - start);
			if (now - start >= prof_seconds)
				break;
		}
		Sleep(5);
		if (SuspendThread(prof_thread_handle) == (DWORD) -1)
			break;
		/* While the thread is suspended, take nothing that could need a
		   lock it holds (the loader lock, the heap, the log): just its
		   registers and a raw copy of the top of its stack. */
		c.ContextFlags = CONTEXT_CONTROL;
		if (GetThreadContext(prof_thread_handle, &c)) {
			static DWORD stack[256];
			SIZE_T got = 0;
			DWORD caller = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID) (ULONG_PTR) c.Esp, stack,
			                       sizeof stack, &got) &&
			    !ReadProcessMemory(GetCurrentProcess(), (LPCVOID) (ULONG_PTR) c.Esp, stack,
			                       64 * 4, &got))
				got = 0;
			ResumeThread(prof_thread_handle);
			if (is_system_module(c.Eip))
				caller = find_caller(stack, (int) (got / 4));
			prof_add(prof, PROF_SLOTS, c.Eip & ~0xfu, caller);
			prof_add(sec, 256, c.Eip & ~0xfu, caller);
			total++;
			sec_total++;
		} else
			ResumeThread(prof_thread_handle);
	}
	if (total) {
		pvr_log("profile: where the loading thread spent the first %u s (%u samples)\n",
		        prof_seconds, total);
		prof_print(prof, PROF_SLOTS, total, 25);
	}
	return 0;
}

static void prof_start(void)
{
	char env[16];
	if (GetEnvironmentVariableA("PVR_PROFILE", env, sizeof env))
		prof_seconds = (unsigned) atoi(env);
	if (!prof_seconds)
		return;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
	                     &prof_thread_handle, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0))
		return;
	CloseHandle(CreateThread(NULL, 0, prof_thread, NULL, 0, NULL));
}

void diag_init(HINSTANCE dll)
{
	char path[MAX_PATH], env[8], *slash;
	SYSTEMTIME t;

	if (GetModuleFileNameA(dll, dll_dir, sizeof dll_dir) && (slash = strrchr(dll_dir, '\\')))
		slash[1] = 0;
	if (GetEnvironmentVariableA("PVR_LOG", env, sizeof env) && env[0] == '0')
		return;
	InitializeCriticalSection(&log_lock);
	if (GetModuleFileNameA(dll, path, sizeof path) && (slash = strrchr(path, '\\'))) {
		slash[1] = 0;
		strcpy(dll_dir, path);
		strcpy(slash + 1, "pvrsgl.log");
		log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		                       FILE_ATTRIBUTE_NORMAL, NULL);
	}
	QueryPerformanceFrequency(&tfreq);
	QueryPerformanceCounter(&t0);
	log_ready = 1;
	GetLocalTime(&t);
	GetModuleFileNameA(NULL, path, sizeof path);
	pvr_log("pvr-sgl-wrapper log, %04d-%02d-%02d %02d:%02d:%02d\nprocess: %s\nsgl.dll at %p\n",
	        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, path, (void *) dll);
	AddVectoredExceptionHandler(1, on_exception);
	prof_start();
}
