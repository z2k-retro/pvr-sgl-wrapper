/*
 * pvros_backend.c - the parts of the original driver that are not in
 * Imagination's release: the DLL entry point and the start-up work of the
 * thin SGL.DLL.
 *
 * The original driver was three pieces. A thin SGL.DLL (new_thin.c, not
 * released) loaded PVROS.DLL, which found the board and loaded the matching
 * SGLMIDx.DLL; SGL.DLL then forwarded every call into it. The wrapper builds
 * SGLMID5 and PVROS from source into one DLL, so the jump tables between
 * them are not needed, and this file does the start-up the thin layer did:
 * find the (emulated) board, create the texture heap and logical device, and
 * hand them to SGL through SglSetGlobal.
 *
 * SPDX-License-Identifier: MIT
 */

#include <windows.h>

#include "sgl_defs.h"
#include "sgl.h"
#include "pvrosapi.h"
#include "vsgl.h"

void diag_init(HINSTANCE dll);

/* PVROS called into SGLMIDx through a jump table (win32/pvrosif.c); here
   they are one DLL, so there is nothing to load. */
int CALL_CONV PVROSAPIInit(void)
{
	return TRUE;
}

void CALL_CONV PVROSAPIExit(void)
{
}

/*
 * sgl_register_api - exported by the original thin SGL.DLL. Judging by the
 * strings in that DLL, PVROS used it to hand SGL.DLL the function table of
 * the SGLMIDx library it had chosen for the installed board. Here SGL is all
 * one DLL, so there is nothing to register; it exists so the export table
 * matches.
 */
void CALL_CONV sgl_register_api(void)
{
}

/* The start-up of the thin SGL.DLL, reconstructed from the interfaces it
   had to use: SglSetGlobal's cases (rnglobal.c) say what SGL expects to be
   given, and PVROS's functions how to get each one. */
static HDEVICE   hPhysDev;
static PVRHANDLE hTexHeap;
static HLDEVICE  hLogDev;

static int setup_device(HINSTANCE hinst)
{
	PciDeviceTable boards[4];
	void *pTexIF = NULL;
	sgl_uint32 n;

	n = PVROSEnumPCIDevices(NULL);
	if (n == 0 || n > 4) {
		PVROSPrintf("pvr-sgl-wrapper: no board\n");
		return FALSE;
	}
	PVROSEnumPCIDevices(boards);

	hPhysDev = PVROSCreatePhysicalDevice(&boards[0]);
	if (!hPhysDev) {
		PVROSPrintf("pvr-sgl-wrapper: could not create the physical device\n");
		return FALSE;
	}
	hTexHeap = PVROSCreateTexHeap(hPhysDev);
	if (!hTexHeap || !PVROSGetTexInterface(hPhysDev, &pTexIF)) {
		PVROSPrintf("pvr-sgl-wrapper: could not set up texture memory\n");
		return FALSE;
	}
	hLogDev = PVROSCreateLogicalDevice(hPhysDev, hTexHeap);
	if (!hLogDev) {
		PVROSPrintf("pvr-sgl-wrapper: could not create the logical device\n");
		return FALSE;
	}

	SglSetGlobal(SGL_DllHandle, hinst);
	SglSetGlobal(SGL_DeviceID, (void *) boards[0].DeviceID);
	SglSetGlobal(SGL_DeviceType, (void *) boards[0].DeviceType);
	/* The texture interface first: SGL_LogicalDev checks the heap against it. */
	SglSetGlobal(SGL_TextureIF, pTexIF);
	SglSetGlobal(SGL_LogicalDev, hLogDev);
	pvr_log("pvr-sgl-wrapper: emulated PCX2 ready\n");
	return TRUE;
}

static void teardown_device(void)
{
	if (hLogDev)
		PVROSDestroyLogicalDevice(hLogDev);
	if (hTexHeap)
		PVROSDestroyTexHeap(hTexHeap);
	if (hPhysDev)
		PVROSDestroyPhysicalDevice(hPhysDev);
	hLogDev = NULL;
	hTexHeap = NULL;
	hPhysDev = NULL;
}

/* The original PVROS.DLL and SGLMID5.DLL each had an entry point
   (win32/system.c and win32/w32dll.c, renamed by the build). Run both,
   PVROS first, then the thin layer's device set-up. */
BOOL WINAPI PVROS_DllMain(HINSTANCE h, DWORD reason, LPVOID reserved);
BOOL WINAPI SGL_DllMain(HINSTANCE h, DWORD reason, LPVOID reserved);

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(h);
		diag_init(h);
		if (!PVROS_DllMain(h, reason, reserved)) {
			pvr_log("pvr-sgl-wrapper: PVROS start-up failed\n");
			return FALSE;
		}
		if (!SGL_DllMain(h, reason, reserved)) {
			pvr_log("pvr-sgl-wrapper: SGL start-up failed\n");
			PVROS_DllMain(h, DLL_PROCESS_DETACH, reserved);
			return FALSE;
		}
		if (!setup_device(h)) {
			SGL_DllMain(h, DLL_PROCESS_DETACH, reserved);
			PVROS_DllMain(h, DLL_PROCESS_DETACH, reserved);
			return FALSE;
		}
		return TRUE;
	case DLL_PROCESS_DETACH:
		/* At process exit (reserved != NULL) other threads are gone and
		   DirectDraw may already be unloaded; leave the clean-up to Windows. */
		if (!reserved)
			teardown_device();
		/* PVROS first: it frees its buffers from the heap that SGL's
		   detach destroys (MemFini). */
		PVROS_DllMain(h, reason, reserved);
		SGL_DllMain(h, reason, reserved);
		return TRUE;
	default:
		return TRUE;
	}
}
