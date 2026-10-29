/*
 * ddraw.c - the DirectDraw path games use: a window, sgl_use_ddraw_mode,
 * a full-screen 640x480x16 double-buffered device, a mip-mapped texture,
 * and a few frames of a spinning textured quad over a Gouraud one. After
 * the last frame it copies the screen to a BMP.
 *
 * Usage: ddraw.exe [frames] [out.bmp]
 *
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sgl.h"

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProcA(h, m, w, l);
}

static void save_screen(const char *path)
{
	HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
	int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
	BITMAPINFO bi;
	BITMAPFILEHEADER fh;
	void *bits;
	HBITMAP bmp;
	FILE *f;

	memset(&bi, 0, sizeof bi);
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 24;
	bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	SelectObject(mem, bmp);
	BitBlt(mem, 0, 0, w, h, screen, 0, 0, SRCCOPY);
	GdiFlush();
	f = fopen(path, "wb");
	if (f) {
		int stride = (w * 3 + 3) & ~3;
		memset(&fh, 0, sizeof fh);
		fh.bfType = 0x4d42;
		fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader;
		fh.bfSize = fh.bfOffBits + stride * h;
		fwrite(&fh, sizeof fh, 1, f);
		fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
		fwrite(bits, stride * h, 1, f);
		fclose(f);
	}
	printf("saved %dx%d screen to %s\n", w, h, path);
}

int main(int argc, char **argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 30;
	const char *out = argc > 2 ? argv[2] : "ddraw.bmp";
	WNDCLASSA wc;
	HWND hwnd;
	SGLCONTEXT ctx;
	SGLVERTEX v[4];
	int quad[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
	int dev, i, k, earliest, latest, tex;
	DWORD t0;
	static sgl_map_pixel pixels[128 * 128];
	sgl_intermediate_map map;

	memset(&wc, 0, sizeof wc);
	wc.lpfnWndProc = wndproc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = "pvrtest";
	RegisterClassA(&wc);
	hwnd = CreateWindowA("pvrtest", "pvr-sgl-wrapper test", WS_POPUP | WS_VISIBLE,
	                     0, 0, 640, 480, NULL, NULL, wc.hInstance, NULL);

	sgl_use_ddraw_mode(hwnd, NULL);
	dev = sgl_create_screen_device(0, 640, 480, sgl_device_16bit, TRUE);
	printf("sgl_create_screen_device: %d\n", dev);
	if (dev < 0)
		return 1;

	/* A 128x128 checkerboard with a coloured gradient, mip-mapped by SGL. */
	for (i = 0; i < 128 * 128; i++) {
		int x = i & 127, y = i >> 7, on = ((x >> 4) ^ (y >> 4)) & 1;
		pixels[i].red = (unsigned char) (on ? 255 : x * 2);
		pixels[i].green = (unsigned char) (on ? 255 : y * 2);
		pixels[i].blue = (unsigned char) (on ? 255 : 64);
		pixels[i].alpha = 0;
	}
	memcpy(map.id, "IMAP", 4);
	map.x_dim = map.y_dim = 128;
	map.pixels = pixels;
	tex = sgl_create_texture(sgl_map_16bit_mm, sgl_map_128x128, sgl_mipmap_generate_2x2,
	                         FALSE, &map, NULL);
	printf("sgl_create_texture: %d\n", tex);

	t0 = GetTickCount();
	for (i = 0; i < frames; i++) {
		float a = i * 0.05f;
		MSG msg;
		while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
			DispatchMessageA(&msg);

		memset(&ctx, 0, sizeof ctx);
		ctx.u32Flags = SGLTT_GOURAUD;
		ctx.cBackgroundColour[0] = 0.0f;
		ctx.cBackgroundColour[1] = 0.2f;
		ctx.cBackgroundColour[2] = 0.1f;
		sgltri_startofframe(&ctx);
		for (k = 0; k < 4; k++) {
			float ang = a + k * 1.5707963f;
			memset(&v[k], 0, sizeof v[k]);
			v[k].fX = 320 + 180 * cosf(ang);
			v[k].fY = 240 + 180 * sinf(ang);
			v[k].fInvW = 0.5f;
			v[k].u32Colour = (k & 1) ? 0xff4060ff : 0xff102040;
		}
		sgltri_triangles(&ctx, 2, quad, v);

		/* A textured quad in front, smaller, turning the other way. */
		ctx.u32Flags = SGLTT_TEXTURE | SGLTT_BILINEAR;
		ctx.nTextureName = tex;
		ctx.eFilterType = sgl_tf_bilinear;
		for (k = 0; k < 4; k++) {
			float ang = -a * 2 + k * 1.5707963f;
			memset(&v[k], 0, sizeof v[k]);
			v[k].fX = 320 + 120 * cosf(ang);
			v[k].fY = 240 + 120 * sinf(ang);
			v[k].fInvW = 0.8f;
			v[k].u32Colour = 0xffffffff;
			v[k].fUOverW = (k == 1 || k == 2) ? 0.8f : 0.0f;
			v[k].fVOverW = (k >= 2) ? 0.8f : 0.0f;
		}
		sgltri_triangles(&ctx, 2, quad, v);
		sgltri_render(&ctx);
	}
	while (sgltri_isrendercomplete(&ctx, 0) != 0 && GetTickCount() - t0 < 5000)
		Sleep(1);
	printf("%d frames in %lu ms\n", frames, GetTickCount() - t0);
	sgl_get_errors(&earliest, &latest);
	printf("errors %d %d\n", earliest, latest);
	save_screen(out);
	sgl_delete_device(dev);
	DestroyWindow(hwnd);
	return 0;
}
