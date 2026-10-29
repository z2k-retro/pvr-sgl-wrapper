/*
 * pvros_backend.c - stand-in for PVROS.DLL.
 *
 * In the original driver, SGLMID5.DLL reached the hardware and the display
 * through PVROS.DLL, loaded at run time and called through a jump table
 * (win32/pvrosif.c). The wrapper links SGLMID5's code directly against this
 * file instead. Every function here is a stub for now: it logs that it was
 * called and returns a neutral value. They get real bodies as the backend
 * (display, texture heap, render scheduling into the PCX2 core) is written.
 *
 * SPDX-License-Identifier: MIT
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "sgl_defs.h"
#include "sgl.h"
#include "pvrosapi.h"

#define STUB() (OutputDebugStringA("pvros stub: "), OutputDebugStringA(__func__), OutputDebugStringA("\n"))

int CALL_CONV PVROSAPIInit(void)
{
	return TRUE;
}

void CALL_CONV PVROSAPIExit(void)
{
}

sgl_uint32 CALL_CONV PVROSGetInterface (HLDEVICE hLDevice, PVR_Interface Spec, PVRHANDLE *ppIf)
{
	STUB();
	return (sgl_uint32) 0;
}

sgl_uint32 CALL_CONV PVROSReleaseInterface (HLDEVICE hLDevice)
{
	STUB();
	return (sgl_uint32) 0;
}

sgl_uint32 CALL_CONV PVROSGetTexInterface (HDEVICE hDeviceID, PVRHANDLE *ppIf)
{
	STUB();
	return (sgl_uint32) 0;
}

void CALL_CONV PVROSPrintf (char* pszFormat,...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, pszFormat);
	vsnprintf(buf, sizeof buf, pszFormat, ap);
	va_end(ap);
	OutputDebugStringA(buf);
}

void CALL_CONV PVROSAssert (char *szAssertText,char *szFileName,sgl_int32 nLine,int * ask)
{
	STUB();
}

sgl_uint32 CALL_CONV PVROSQueryPerformanceCounter (sgl_largeint* Val)
{
	return QueryPerformanceCounter((LARGE_INTEGER *) Val);
}

sgl_uint32 CALL_CONV PVROSQueryPerformanceFrequency (sgl_largeint* Val)
{
	return QueryPerformanceFrequency((LARGE_INTEGER *) Val);
}

int CALL_CONV PVROSGetUsagePhysicalDevice (PciDeviceTable *pBoard)
{
	STUB();
	return (int) 0;
}

HDEVICE CALL_CONV PVROSCreatePhysicalDevice (PciDeviceTable *pBoard)
{
	STUB();
	return (HDEVICE) 0;
}

void CALL_CONV PVROSDestroyPhysicalDevice (HDEVICE hDeviceID)
{
	STUB();
}

PVRHANDLE CALL_CONV PVROSCreateTexHeap (HDEVICE hDeviceID)
{
	STUB();
	return (PVRHANDLE) 0;
}

void CALL_CONV PVROSDestroyTexHeap (PVRHANDLE TexHeap)
{
	STUB();
}

HLDEVICE CALL_CONV PVROSCreateLogicalDevice (HDEVICE hDeviceID,PVRHANDLE TexHeap)
{
	STUB();
	return (HLDEVICE) 0;
}

void CALL_CONV PVROSDestroyLogicalDevice (HLDEVICE Device)
{
	STUB();
}

HDISPLAY CALL_CONV PVROSOpenDisplayDevice (void *pLogDevice, DISPLAYID DisplayID,FRAME_BUFFER_MODE fbm)
{
	STUB();
	return (HDISPLAY) 0;
}

PVROSERR CALL_CONV PVROSCloseDisplayDevice (void *pLogDevice, HDISPLAY hDisplay)
{
	STUB();
	return (PVROSERR) 0;
}

sgl_uint32 CALL_CONV PVROSEnumDisplayModes (HDISPLAY hDisplay,DisplayModeTable *DMT)
{
	STUB();
	return (sgl_uint32) 0;
}

PVROSERR CALL_CONV PVROSSelectDisplayMode (HDISPLAY hDisplay,HMODE hMode,sgl_uint32 nBuffers)
{
	STUB();
	return (PVROSERR) 0;
}

PVROSERR CALL_CONV PVROSCallbackRegister (void *pLogDevice,CALLBACK_TYPE cType,PVROSCB fn,void *pContext)
{
	STUB();
	return (PVROSERR) 0;
}

PVROSERR CALL_CONV PVROSCallback (void *pLogDevice,CALLBACK_TYPE cType,void *pCallbackData)
{
	STUB();
	return (PVROSERR) 0;
}

void * CALL_CONV PVROSMalloc (sgl_uint32 X)
{
	return malloc(X);
}

void CALL_CONV PVROSFree (void * X)
{
	free(X);
}

PVROSERR CALL_CONV PVROSScheduleRender (HLDEVICE LogicalDevice)
{
	STUB();
	return (PVROSERR) 0;
}

sgl_uint32 CALL_CONV PVROSDetermineTexMemConfig (sgl_uint32 fSettings)
{
	STUB();
	return (sgl_uint32) 0;
}

sgl_uint32 CALL_CONV PVROSEnumPCIDevices (PciDeviceTable *pPDT)
{
	STUB();
	return (sgl_uint32) 0;
}

sgl_uint32 CALL_CONV PVROSExtendTSPBuffer (HLDEVICE gHLogicalDev)
{
	STUB();
	return (sgl_uint32) 0;
}

sgl_int32 CALL_CONV HWRdValFileInt (char * RegName,sgl_int32 DefaultVal)
{
	return DefaultVal;
}

sgl_uint32 CALL_CONV HWRdValFileUInt (char * RegName,sgl_uint32 DefaultVal)
{
	return DefaultVal;
}

float CALL_CONV HWRdValFileFloat (char * RegName,float DefaultVal)
{
	return DefaultVal;
}

sgl_bool CALL_CONV HWRdValFileString (char * RegName, char *pszBuffer,int pszBufferSize)
{
	STUB();
	return (sgl_bool) 0;
}

sgl_bool CALL_CONV GetApplicationHintFloat (char *pszHint,float *pData)
{
	STUB();
	return (sgl_bool) 0;
}

sgl_bool CALL_CONV GetApplicationHintInt (char *pszHint,sgl_int32 *pData)
{
	STUB();
	return (sgl_bool) 0;
}

sgl_bool CALL_CONV GetApplicationHintString (char *pszHint,char *pszBuffer,int pszBufferSize)
{
	STUB();
	return (sgl_bool) 0;
}

int CALL_CONV SglReadPrivateProfileInt (char *szSection,char *szEntry,int nDefault,char *szFilename)
{
	return nDefault;
}

sgl_bool CALL_CONV SglWritePrivateProfileInt (char *szSection,char *szEntry,int nValue,char *szFilename)
{
	STUB();
	return (sgl_bool) 0;
}

sgl_bool CALL_CONV SglReadPrivateProfileString (char *szSection,char *szEntry,char *szDefault,char *szText,int nTextSize,char *szFilename)
{
	STUB();
	return (sgl_bool) 0;
}

sgl_bool CALL_CONV SglWritePrivateProfileString (char *szSection,char *szEntry,char *szText,char *szFilename)
{
	STUB();
	return (sgl_bool) 0;
}

float CALL_CONV SglReadPrivateProfileFloat (char *szSection, char *szEntry, float fDefault, char *szFilename)
{
	return fDefault;
}

void CALL_CONV PVROSSetDDrawWindowHandle (void *hWnd)
{
	STUB();
}

PVROSERR CALL_CONV GetDDraw2dCallbackInfo (HDISPLAY hDisplay,void *pCSP)
{
	STUB();
	return (PVROSERR) 0;
}

PVROSERR CALL_CONV PVROSAssignVirtualBuffers (PVR_PARAM_BUFF *pPVRParamBuf, HLDEVICE HLogicalDev)
{
	STUB();
	return (PVROSERR) 0;
}

void CALL_CONV PVROSFreeVirtualBuffers (HLDEVICE hLogDev)
{
	STUB();
}

char * CALL_CONV PVROSDbgDevBuffer (int k)
{
	STUB();
	return (char *) 0;
}
