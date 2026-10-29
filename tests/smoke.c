/*
 * smoke.c - load SGL.DLL by ordinal, the way the original sgl.lib linked
 * games to it, check the names Unreal and the MiniGLs look up at run time, and make the
 * first calls a game would make. With the stub
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

/* Every function looked up with GetProcAddress by Unreal's SGLDrv.dll and by
   the PowerVR MiniGLs for Quake and Quake II (all three load SGL.DLL at run
   time). */
static const char *lookup_names[] = {
	"sgl_create_screen_device", "sgl_create_texture", "sgl_delete_device",
	"sgl_delete_texture", "sgl_direct_set_texture", "sgl_get_device",
	"sgl_get_errors", "sgl_get_free_texture_mem", "sgl_get_free_texture_mem_info",
	"sgl_get_ini_int",
	"sgl_get_ini_string", "sgl_get_versions", "sgl_get_win_versions",
	"sgl_preprocess_texture", "sgl_rand", "sgl_set_texture",
	"sgl_set_texture_extended", "sgl_srand",
	"sgl_texture_size", "sgl_use_address_mode", "sgl_use_ddraw_mode",
	"sgl_use_eor_callback", "sgltri_isrendercomplete", "sgltri_lines",
	"sgltri_points", "sgltri_quads", "sgltri_render", "sgltri_rerender",
	"sgltri_shadow", "sgltri_startofframe", "sgltri_triangles",
};

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

	{
		int i, missing = 0;
		for (i = 0; i < (int) (sizeof lookup_names / sizeof *lookup_names); i++)
			if (!GetProcAddress(h, lookup_names[i])) {
				printf("missing by name: %s\n", lookup_names[i]);
				missing++;
			}
		printf("%d run-time lookups by name (Unreal, MiniGLs): %d missing\n",
		       (int) (sizeof lookup_names / sizeof *lookup_names), missing);
	}

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
