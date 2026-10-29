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

static HANDLE log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_lock;
static int log_ready, crashes_logged;

static void log_raw(const char *s)
{
	DWORD n;
	if (!log_ready)
		return;
	EnterCriticalSection(&log_lock);
	if (log_file != INVALID_HANDLE_VALUE)
		WriteFile(log_file, s, (DWORD) strlen(s), &n, NULL);
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

void diag_init(HINSTANCE dll)
{
	char path[MAX_PATH], env[8], *slash;
	SYSTEMTIME t;

	if (GetEnvironmentVariableA("PVR_LOG", env, sizeof env) && env[0] == '0')
		return;
	InitializeCriticalSection(&log_lock);
	if (GetModuleFileNameA(dll, path, sizeof path) && (slash = strrchr(path, '\\'))) {
		strcpy(slash + 1, "pvrsgl.log");
		log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		                       FILE_ATTRIBUTE_NORMAL, NULL);
	}
	log_ready = 1;
	GetLocalTime(&t);
	GetModuleFileNameA(NULL, path, sizeof path);
	pvr_log("pvr-sgl-wrapper log, %04d-%02d-%02d %02d:%02d:%02d\nprocess: %s\nsgl.dll at %p\n",
	        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, path, (void *) dll);
	AddVectoredExceptionHandler(1, on_exception);
}
