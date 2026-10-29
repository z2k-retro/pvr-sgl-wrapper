/*
 * smoke.c - load SGL.DLL by ordinal, the way the original sgl.lib linked
 * games to it, and make the first calls a game would make. With the stub
 * backend the screen device cannot be created yet; the point is that the
 * library loads, initialises and reaches the backend without crashing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <stdio.h>

typedef struct { char *library; char *required_header; } sgl_versions;
typedef sgl_versions *(__cdecl *PFN_VERSIONS)(void);
typedef int (__cdecl *PFN_CREATE_SCREEN)(int, int, int, int, int);

int main(void)
{
	HMODULE h = LoadLibraryA("sgl.dll");
	PFN_VERSIONS versions;
	PFN_CREATE_SCREEN create;
	sgl_versions *v;
	int dev;

	if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
	versions = (PFN_VERSIONS) GetProcAddress(h, MAKEINTRESOURCEA(1));
	create = (PFN_CREATE_SCREEN) GetProcAddress(h, MAKEINTRESOURCEA(2));
	if (!versions || !create) { printf("missing ordinal 1 or 2\n"); return 1; }

	v = versions();
	printf("sgl_get_versions: library \"%s\", header \"%s\"\n",
	       v && v->library ? v->library : "(null)",
	       v && v->required_header ? v->required_header : "(null)");

	/* device 0, 640x480, sgl_device_16bit (1), double buffered */
	dev = create(0, 640, 480, 1, 1);
	printf("sgl_create_screen_device: %d\n", dev);
	FreeLibrary(h);
	return 0;
}
