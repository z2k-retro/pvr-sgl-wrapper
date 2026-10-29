/*
 * tri.c - draw a frame through SGL.DLL into memory and save it as a BMP.
 *
 * Uses SGL's address mode (sgl_use_address_mode), where the application
 * says where each frame goes, so no window or DirectDraw is needed. The
 * scene: a background colour, a Gouraud-shaded triangle, and a translucent
 * quad crossing it in front.
 *
 * The PCX2 shades smoothly in intensity only (one colour per polygon, see
 * the reference, Appendix B), so the triangle's vertices differ in
 * brightness rather than hue.
 *
 * Usage: tri.exe [out.bmp]
 *
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "sgl.h"

#define W 640
#define H 480

static unsigned short frame[H][W];
static int frames_given;

static int CALL_CONV next_address(P_CALLBACK_ADDRESS_PARAMS p)
{
	p->pMem = frame;
	p->wStride = W * 2;
	p->bBitsPerPixel = 16;
	frames_given++;
	return sgl_no_err;
}

static void save_bmp(const char *path)
{
	FILE *f = fopen(path, "wb");
	BITMAPFILEHEADER fh = { 0 };
	BITMAPINFOHEADER ih = { 0 };
	int x, y;

	if (!f)
		return;
	fh.bfType = 0x4d42;
	fh.bfOffBits = sizeof fh + sizeof ih;
	fh.bfSize = fh.bfOffBits + W * H * 3;
	ih.biSize = sizeof ih;
	ih.biWidth = W;
	ih.biHeight = -H;
	ih.biPlanes = 1;
	ih.biBitCount = 24;
	fwrite(&fh, sizeof fh, 1, f);
	fwrite(&ih, sizeof ih, 1, f);
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			unsigned p = frame[y][x]; /* RGB 565 */
			unsigned char bgr[3];
			bgr[0] = (unsigned char) ((p & 0x1f) << 3);
			bgr[1] = (unsigned char) (((p >> 5) & 0x3f) << 2);
			bgr[2] = (unsigned char) (((p >> 11) & 0x1f) << 3);
			fwrite(bgr, 3, 1, f);
		}
	fclose(f);
}

static SGLVERTEX vtx(float x, float y, float invw, unsigned colour)
{
	SGLVERTEX v;
	memset(&v, 0, sizeof v);
	v.fX = x;
	v.fY = y;
	v.fInvW = invw;
	v.u32Colour = colour;
	return v;
}

int main(int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "tri.bmp";
	sgl_uint32 *status = NULL;
	SGLCONTEXT ctx;
	SGLVERTEX v[7];
	int tri[1][3] = { { 0, 1, 2 } };
	int quad[2][3] = { { 3, 4, 5 }, { 3, 5, 6 } };
	int dev, wait, earliest, latest;

	memset(frame, 0x55, sizeof frame); /* untouched pixels stay this grey */

	if (sgl_use_address_mode(next_address, &status) != sgl_no_err) {
		printf("sgl_use_address_mode failed\n");
		return 1;
	}
	dev = sgl_create_screen_device(0, W, H, sgl_device_16bit, FALSE);
	printf("sgl_create_screen_device: %d\n", dev);
	if (dev < 0)
		return 1;

	memset(&ctx, 0, sizeof ctx);
	ctx.u32Flags = SGLTT_GOURAUD;
	ctx.cBackgroundColour[0] = 0.1f;
	ctx.cBackgroundColour[1] = 0.1f;
	ctx.cBackgroundColour[2] = 0.3f;
	ctx.eFilterType = sgl_tf_point_sample;
	sgltri_startofframe(&ctx);

	v[0] = vtx(320, 60, 0.5f, 0xffffc040);
	v[1] = vtx(560, 420, 0.5f, 0xff302010);
	v[2] = vtx(80, 420, 0.5f, 0xff906020);
	sgltri_triangles(&ctx, 1, tri, v);

	/* Nearer (larger 1/w), flat white, half translucent. */
	ctx.u32Flags = SGLTT_GLOBALTRANS;
	ctx.u32GlobalTrans = 128;
	v[3] = vtx(200, 200, 0.9f, 0xffffffff);
	v[4] = vtx(600, 200, 0.9f, 0xffffffff);
	v[5] = vtx(600, 300, 0.9f, 0xffffffff);
	v[6] = vtx(200, 300, 0.9f, 0xffffffff);
	sgltri_triangles(&ctx, 2, quad, v);

	sgltri_render(&ctx);
	for (wait = 0; wait < 200 && status && !(*status & 1); wait++)
		Sleep(10);
	sgl_get_errors(&earliest, &latest);
	printf("frames given: %d, status %s, errors %d %d\n", frames_given,
	       status && (*status & 1) ? "end of render" : "still rendering", earliest, latest);

	save_bmp(out);
	printf("pixel at centre of triangle: %04x, background corner: %04x\n",
	       frame[300][320], frame[10][10]);
	sgl_delete_device(dev);
	return 0;
}
