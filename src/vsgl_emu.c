/*
 * vsgl_emu.c - stand-in for the PowerVR VxD (VSGL.VXD), emulating one PCX2.
 *
 * In the original driver, PVROS (win32/system.c) asked the VxD for memory,
 * board details and renders through DeviceIoControl. This file answers those
 * requests inside the process, for one emulated PCX2 (a Matrox m3D):
 *
 *  - The board's two memory windows are ordinary allocations: a 4 KB
 *    register file (BAR0) and 4 MB of texture memory (BAR1). SGL writes
 *    textures straight into the second, as it did into the card.
 *  - "Physical" addresses are the linear addresses themselves. Nothing
 *    leaves the process, so no translation is needed.
 *  - SCHEDULE_RENDER does what the VxD did (copy the TSP parameters into
 *    texture memory, take the register shadow) and then, in place of the
 *    chip, runs the PCX2 renderer from the 86Box emulation and writes the
 *    pixels to the surface SGL gave as SOFADDR.
 *
 * The sequence follows the PCX2 reference (github.com/z2k-retro/
 * powervr-pcx2-tools, docs/reference.md and Appendix F).
 *
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sgl_defs.h"
#include "sgl.h"
#include "pvrosapi.h"
#include "pci.h"
#include "hwregs.h"
#include "vsgl.h"
#include "pcx2/vid_powervr_pcx_render.h"
#include "pcx2/vid_powervr_pcx_tables.h"

#define VSGL_HANDLE ((HANDLE) (ULONG_PTR) 0x56534731) /* "VSG1" */

/* NEC PowerVR PCX2 (rev 1, as on the Matrox m3D). */
#define PCX2_VENDOR_ID  0x1033
#define PCX2_DEVICE_ID  0x0046
#define PCX2_REVISION   1
#define PCX2_PCI_ID     0x0108 /* bus 1, device 1, function 0; must not be 0 */
#define PCX2_BOARD_ID   1

#define REG_BYTES       (0x400 * 4)
#define TEXMEM_BYTES    (4u << 20)

static uint32_t *board_regs;
static uint32_t *board_tex;
static volatile PVR_STATUS board_status;
static uint32_t *frame_rgb;

/* Render helpers: the tiles of a frame are shared out between this thread
   and up to MAX_HELPERS others (one fewer than the CPU's threads). */
#define MAX_HELPERS 15
static pcxr_job_t *render_job;
static int     n_helpers;
static HANDLE  helper_go[MAX_HELPERS], helper_done[MAX_HELPERS];

static DWORD WINAPI helper_main(LPVOID arg)
{
	int i = (int) (INT_PTR) arg;
	for (;;) {
		WaitForSingleObject(helper_go[i], INFINITE);
		pcxr_render_tiles(render_job, frame_rgb, 0, 0);
		SetEvent(helper_done[i]);
	}
	return 0;
}

static void helpers_init(void)
{
	SYSTEM_INFO si;
	int i, want;
	char env[16];

	render_job = pcxr_job_create();
	GetSystemInfo(&si);
	want = (int) si.dwNumberOfProcessors - 1;
	if (GetEnvironmentVariableA("PVR_RENDER_THREADS", env, sizeof env))
		want = atoi(env) - 1;
	if (want > MAX_HELPERS)
		want = MAX_HELPERS;
	for (i = 0; i < want; i++) {
		helper_go[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
		helper_done[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
		if (!CreateThread(NULL, 0, helper_main, (LPVOID) (INT_PTR) i, 0, NULL))
			break;
		n_helpers++;
	}
}

/* Draw a frame on all helpers; falls back to one thread if none started. */
static void render_frame(const pcxr_frame_t *f, pcxr_result_t *r)
{
	int i;

	if (!render_job) {
		pcxr_render(f, frame_rgb, r);
		return;
	}
	if (pcxr_prepare(render_job, f, r) <= 0)
		return;
	for (i = 0; i < n_helpers; i++)
		SetEvent(helper_go[i]);
	pcxr_render_tiles(render_job, frame_rgb, 0, 0);
	if (n_helpers)
		WaitForMultipleObjects(n_helpers, helper_done, TRUE, INFINITE);
}

#define vlog pvr_log

static int board_init(void)
{
	if (board_regs)
		return 1;
	board_regs = VirtualAlloc(NULL, REG_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	board_tex = VirtualAlloc(NULL, TEXMEM_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	frame_rgb = VirtualAlloc(NULL, PCXR_MAX_W * PCXR_MAX_H * 4, MEM_COMMIT | MEM_RESERVE,
	                         PAGE_READWRITE);
	if (!board_regs || !board_tex || !frame_rgb)
		return 0;
	board_regs[PCX_REVISION] = PCX2_REVISION;
	helpers_init();
	board_status = PVR_STATUS_EOR;
	return 1;
}

static void board_info(BoardDataBlock *b)
{
	memset(b, 0, sizeof *b);
	b->DeviceType = MIDAS5;
	b->PCIVendorID = PCX2_VENDOR_ID;
	b->PCIDeviceID = PCX2_DEVICE_ID;
	b->PCIBus = PCX2_PCI_ID >> 8;
	b->PCIDev = (PCX2_PCI_ID >> 3) & 0x1f;
	b->PCIFunc = PCX2_PCI_ID & 7;
	b->RevisionID = PCX2_REVISION;
	b->IRQ = 11;
	b->nMemWindows = 2;
	b->PhysMemWindows[0][0] = (sgl_uint32) board_regs;
	b->PhysMemWindows[0][1] = REG_BYTES;
	b->PhysMemWindows[1][0] = (sgl_uint32) board_tex;
	b->PhysMemWindows[1][1] = TEXMEM_BYTES;
	b->LinearMemWindows[0] = (sgl_uint32) board_regs;
	b->LinearMemWindows[1] = (sgl_uint32) board_tex;
	b->pStatus = &board_status;
}

/* ---------------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------------- */

/* Pixels the chip wrote, packed to PACKMODE, go to SOFADDR. Pixels it did
   not write (outside the tiles, or showing only the masking plane) leave
   the surface as it was, as on the card. */
static void write_frame(const pcxr_result_t *r, uint32_t xclip, uint32_t sofaddr,
                        uint32_t stride, uint32_t packmode)
{
	int bpp = ((packmode & 3) == 0) ? 4 : ((packmode & 3) == 1) ? 3 : 2;
	int x0 = r->min_x, x1 = r->max_x, y;

	if (!sofaddr || r->error || r->max_x < 0 || r->max_y < 0)
		return;
	if ((xclip & (1u << 12)) && x0 < (int) (xclip & 0x3ff) - 1)
		x0 = (int) (xclip & 0x3ff) - 1;
	if ((xclip & (1u << 28)) && x1 > (int) ((xclip >> 16) & 0x3ff) - 1)
		x1 = (int) ((xclip >> 16) & 0x3ff) - 1;
	if (x1 >= PCXR_MAX_W)
		x1 = PCXR_MAX_W - 1;
	for (y = r->min_y; y <= r->max_y && y < PCXR_MAX_H; y++) {
		const uint32_t *src = &frame_rgb[y * PCXR_MAX_W];
		uint8_t *row = (uint8_t *) sofaddr + (size_t) y * stride;
		int x = x0;
		while (x <= x1) {
			int start;
			while (x <= x1 && !(src[x] & PCXR_WRITTEN))
				x++;
			if (x > x1)
				break;
			start = x;
			while (x <= x1 && (src[x] & PCXR_WRITTEN))
				x++;
			pcxr_pack_run(packmode, src, start, x - start, y, row + start * bpp);
		}
	}
}

/* F12 or Scroll Lock (or the virtual-key code in PVR_SHOT_KEY) saves the
   frame just drawn, exactly as the chip wrote it to
   the screen surface (after PACKMODE packing and dithering), as
   pvrshot_NNN.bmp next to sgl.dll. Screen captures made by Windows tools
   can miss a full-screen DirectDraw game; this cannot. */
const char *pvr_dll_dir(void);

static void save_shot(const pcxr_result_t *r, uint32_t sofaddr, uint32_t stride, uint32_t packmode)
{
	static int n;
	char path[MAX_PATH + 32];
	int w = r->max_x + 1, h = r->max_y + 1, x, y, row = 0;
	int bpp = ((packmode & 3) == 0) ? 4 : ((packmode & 3) == 1) ? 3 : 2;
	BITMAPFILEHEADER fh;
	BITMAPINFOHEADER ih;
	unsigned char *line;
	FILE *f;

	if (!sofaddr || w <= 0 || h <= 0)
		return;
	row = (w * 3 + 3) & ~3;
	line = calloc(1, row);
	_snprintf(path, sizeof path, "%spvrshot_%03d.bmp", pvr_dll_dir(), n++);
	f = fopen(path, "wb");
	if (!f || !line) {
		free(line);
		if (f)
			fclose(f);
		return;
	}
	memset(&fh, 0, sizeof fh);
	memset(&ih, 0, sizeof ih);
	fh.bfType = 0x4d42;
	fh.bfOffBits = sizeof fh + sizeof ih;
	fh.bfSize = fh.bfOffBits + row * h;
	ih.biSize = sizeof ih;
	ih.biWidth = w;
	ih.biHeight = -h;
	ih.biPlanes = 1;
	ih.biBitCount = 24;
	fwrite(&fh, sizeof fh, 1, f);
	fwrite(&ih, sizeof ih, 1, f);
	for (y = 0; y < h; y++) {
		const uint8_t *src = (const uint8_t *) sofaddr + (size_t) y * stride;
		for (x = 0; x < w; x++) {
			const uint8_t *p = src + x * bpp;
			unsigned r8, g8, b8;
			if (bpp >= 3) {
				b8 = p[0]; g8 = p[1]; r8 = p[2];
			} else {
				unsigned v = p[0] | (p[1] << 8);
				if ((packmode & 3) == 2) {
					r8 = (v >> 11) << 3; g8 = ((v >> 5) & 0x3f) << 2; b8 = (v & 0x1f) << 3;
				} else {
					r8 = ((v >> 10) & 0x1f) << 3; g8 = ((v >> 5) & 0x1f) << 3; b8 = (v & 0x1f) << 3;
				}
			}
			line[x * 3] = (unsigned char) b8;
			line[x * 3 + 1] = (unsigned char) g8;
			line[x * 3 + 2] = (unsigned char) r8;
		}
		fwrite(line, row, 1, f);
	}
	fclose(f);
	free(line);
	vlog("vsgl: saved %s (%dx%d, PACKMODE 0x%02x)\n", path, w, h, packmode);
}

static BOOL schedule_render(HLDEVICE ld, BUFFER_LIST *bl)
{
	pcxr_frame_t f;
	pcxr_result_t r;
	uint32_t page_bytes, i;
	const uint32_t *regs = bl->Registers;

	/* 1. Copy any buffer that does not already sit where the chip reads it.
	      In practice only the TSP parameters move, into texture memory. */
	for (i = 0; i < 3; i++) {
		PVR_PARAM_BUFF *pb = &bl->PVRParamBuf[i];
		if (bl->DestAddr[i] && pb->pBuffer && bl->DestAddr[i] != pb->pBuffer && pb->uBufferPos)
			memcpy(bl->DestAddr[i], pb->pBuffer, pb->uBufferPos * 4);
	}

	/* 2. The register shadow; the ISP buffer as the TLB maps it. */
	page_bytes = 4096u << bl->PCXBuffer.PageSize;
	memset(&f, 0, sizeof f);
	f.isp = (const uint32_t *) bl->PCXBuffer.LinearAddress;
	f.isp_words = bl->PCXBuffer.SlotsUsed * page_bytes / 4;
	f.tex = board_tex;
	f.tex_words = TEXMEM_BYTES / 4;
	f.object_offset = regs[PCX_OBJECT_OFFSET];
	f.fog_shift = regs[PCX_FOGAMOUNT];
	f.fog_colour = regs[PCX_FOGCOL];
	f.camera = regs[PCX_CAMERA];
	f.xclip = regs[PCX_XCLIP];
	f.bilinear = regs[PCX_BILINEAR];
	f.fastfog = regs[PCX_FASTFOG];
	f.fog_table = pcx2_fog_rom;

	/* 3. In place of the chip: draw the frame and write it out. */
	memset(frame_rgb, 0, PCXR_MAX_W * PCXR_MAX_H * 4);
	memset(&r, 0, sizeof r);
	r.max_x = r.max_y = -1;
	render_frame(&f, &r);
	write_frame(&r, f.xclip, regs[PCX_SOFADDR], regs[PCX_LSTRIDE], regs[PCX_PACKMODE]);
	if (r.error)
		vlog("vsgl: render stopped on a malformed tile list\n");
	{
		/* PVR_SHOT_FRAME=n also saves frame n, for scripted comparisons. */
		static int key_was_down, frame_no, shot_frame = -2, shot_key;
		int down;
		if (shot_frame == -2) {
			char env[16];
			shot_frame = GetEnvironmentVariableA("PVR_SHOT_FRAME", env, sizeof env) ? atoi(env) : -1;
			shot_key = GetEnvironmentVariableA("PVR_SHOT_KEY", env, sizeof env) ?
			           (int) strtol(env, NULL, 0) : 0;
		}
		if (shot_key)
			down = (GetAsyncKeyState(shot_key) & 0x8000) != 0;
		else
			down = ((GetAsyncKeyState(VK_F12) | GetAsyncKeyState(VK_SCROLL)) & 0x8000) != 0;
		if ((down && !key_was_down) || frame_no++ == shot_frame)
			save_shot(&r, regs[PCX_SOFADDR], regs[PCX_LSTRIDE], regs[PCX_PACKMODE]);
		key_was_down = down;
	}

	/* 4. End of render, as the VxD's interrupt handler reported it, and
	      the buffer goes back to the pool (BUFFER_LIST.bInUse, offset 0,
	      which only the VxD cleared). */
	bl->bInUse = FALSE;
	board_status = PVR_STATUS_EOR;
	ld->RenderStatus = PVR_STATUS_EOR;
	if (ld->RendersPending)
		ld->RendersPending--;
	return TRUE;
}

/* ---------------------------------------------------------------------
 * DeviceIoControl
 * ------------------------------------------------------------------- */

HANDLE vsgl_open(const char *name)
{
	(void) name;
	if (!board_init())
		return INVALID_HANDLE_VALUE;
	return VSGL_HANDLE;
}

BOOL vsgl_close(HANDLE h)
{
	if (h == VSGL_HANDLE)
		return TRUE;
	return CloseHandle(h);
}

#define IN32(n)  (((DWORD *) in)[n])
#define OUT32(n) (((DWORD *) out)[n])

BOOL vsgl_ioctl(HANDLE h, DWORD code, void *in, DWORD in_size,
                void *out, DWORD out_size, DWORD *returned, void *overlapped)
{
	DWORD dummy;

	(void) overlapped;
	if (!returned)
		returned = &dummy;
	*returned = 0;
	if (h != VSGL_HANDLE || !board_init())
		return FALSE;

	switch (code) {
	case VSGL_GET_VERSION: /* {minor, major, ...} bytes */
		if (out_size >= 2) {
			memset(out, 0, out_size);
			((BYTE *) out)[0] = 1;
			((BYTE *) out)[1] = 4;
			*returned = out_size;
		}
		return TRUE;

	case VSGL_GET_MAN_TYPE: /* 1 = Matrox */
		if (out_size >= 4)
			OUT32(0) = 1;
		return TRUE;

	case VSGL_ENUM_PHYSICAL_BOARDS:
		/* Called first with room for the count, then for the table. */
		if (out_size < sizeof(PciDeviceTable)) {
			if (out_size >= 4)
				OUT32(0) = 1;
		} else {
			PciDeviceTable *t = out;
			t->DeviceID = PCX2_PCI_ID;
			t->PCIVendorID = PCX2_VENDOR_ID;
			t->PCIDeviceID = PCX2_DEVICE_ID;
			t->DeviceType = MIDAS5;
		}
		return TRUE;

	case VSGL_GET_PHYSICAL_BOARD:
		if (IN32(0) != PCX2_PCI_ID)
			return FALSE;
		OUT32(0) = PCX2_BOARD_ID;
		return TRUE;

	case VSGL_GET_PHYSICAL_BOARD_INFO:
		if (out_size < sizeof(BoardDataBlock))
			return FALSE;
		board_info(out);
		*returned = sizeof(BoardDataBlock);
		return TRUE;

	case VSGL_RELEASE_PHYSICAL_BOARD:
	case VSGL_ENABLE_PHYSICAL_BOARD:
	case VSGL_DISABLE_PHYSICAL_BOARD:
	case VSGL_SCHEDULER_LOCK:
	case VSGL_SCHEDULER_UNLOCK:
	case VSGL_SETCACHEMODE:
	case VSGL_WBINVD:
		return TRUE;

	case VSGL_GETPHYSADDRESS: /* linear in, physical out: the same here */
		OUT32(0) = IN32(0);
		return TRUE;

	case VSGL_ALLOCATEBLOCK: { /* pages in, SHAREDBLOCK out */
		SHAREDBLOCK *b = out;
		void *p = VirtualAlloc(NULL, IN32(0) << 12, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!p)
			return FALSE;
		b->GlobalAlias = b->Ring0Linear = b->PhysicalAddress = (unsigned long) p;
		b->Pages = IN32(0);
		*returned = sizeof *b;
		return TRUE;
	}
	case VSGL_FREEBLOCK:
		VirtualFree((void *) ((SHAREDBLOCK *) in)->GlobalAlias, 0, MEM_RELEASE);
		return TRUE;

	case VSGL_ALLOCPHYSALIAS: { /* physical is linear here */
		SHAREDBLOCK *b = out;
		b->GlobalAlias = b->Ring0Linear = ((SHAREDBLOCK *) in)->PhysicalAddress;
		return TRUE;
	}
	case VSGL_FREEPHYSALIAS:
		return TRUE;

	case VSGL_COPYBUFFER: /* {src, dst, bytes, check} */
		memcpy((void *) IN32(1), (void *) IN32(0), IN32(2));
		return TRUE;

	case VSGL_CREATE_DMA_SCATTER_BUFFER: { /* bytes in, DMASCATTERBUFFER out */
		/* One contiguous run, so the ISP buffer uses 16 KB TLB pages and
		   reads back in the order SGL wrote it. */
		DMASCATTERBUFFER *sb = out;
		DWORD bytes = (IN32(0) + 0x3fff) & ~0x3fffu;
		static int n_created;
		vlog("vsgl: parameter buffer %d, %lu KB\n", ++n_created, bytes >> 10);
		void *p = VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!p || out_size < sizeof *sb)
			return FALSE;
		memset(sb, 0, sizeof *sb);
		sb->LinearAddress = (unsigned long) p;
		sb->PagesAllocated = bytes >> 12;
		sb->BlockSize = sb->PagesAllocated;
		sb->SlotsUsed = 1;
		sb->PhysAddress[0] = (unsigned long) p;
		sb->PhysPages[0] = sb->PagesAllocated;
		*returned = sizeof *sb;
		return TRUE;
	}
	case VSGL_DESTROY_DMA_SCATTER_BUFFER:
		VirtualFree((void *) ((DMASCATTERBUFFER *) in)->LinearAddress, 0, MEM_RELEASE);
		return TRUE;

	case VSGL_ALLOC_VIRTUAL_PAGES: { /* pages in, pointer out */
		void *p = VirtualAlloc(NULL, IN32(0) << 12, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		OUT32(0) = (DWORD) p;
		return p != NULL;
	}
	case VSGL_FREE_VIRTUAL_PAGES:
		VirtualFree((void *) IN32(0), 0, MEM_RELEASE);
		return TRUE;

	case VSGL_PAGE_RESERVE: { /* pages in, linear out */
		void *p = VirtualAlloc(NULL, IN32(0) << 12, MEM_RESERVE, PAGE_READWRITE);
		if (out_size >= 4)
			OUT32(0) = (DWORD) p;
		return p != NULL;
	}
	case VSGL_PAGE_COMMIT: /* {first page, pages} */
		if (!VirtualAlloc((void *) (IN32(0) << 12), IN32(1) << 12, MEM_COMMIT, PAGE_READWRITE))
			return FALSE;
		if (out_size >= 4)
			OUT32(0) = IN32(0) << 12;
		return TRUE;

	case VSGL_PAGE_REALLOCATE: /* {address, pages}: grow inside the reservation */
		if (!VirtualAlloc((void *) IN32(0), IN32(1) << 12, MEM_COMMIT, PAGE_READWRITE))
			return FALSE;
		if (out_size >= 4)
			OUT32(0) = IN32(0);
		return TRUE;

	case VSGL_PAGE_DECOMMIT:
		return TRUE;

	case VSGL_PAGE_FREE:
		VirtualFree((void *) IN32(0), 0, MEM_RELEASE);
		if (out_size >= 4)
			OUT32(0) = 1;
		return TRUE;

	case VSGL_SCHEDULE_RENDER: /* {LDEVICE *, BUFFER_LIST *} */
		return schedule_render((HLDEVICE) IN32(0), (BUFFER_LIST *) IN32(1));

	default:
		vlog("vsgl: unhandled ioctl 0x%lx (in %lu, out %lu)\n", code, in_size, out_size);
		return FALSE;
	}
}

/* ---------------------------------------------------------------------
 * PCI configuration space (replaces win32/pci.asm). PVROS only uses these
 * to tune Intel 430FX chipsets and to probe for a PC-98; there is no PCI
 * bus to talk to. Reads succeed and return all ones (what an empty slot
 * reads as), writes are ignored, and no board is ever found.
 * ------------------------------------------------------------------- */

#define PCI_DEVICE_NOT_FOUND 0x86

sgl_int16 wIsPC98 = 0;

int __cdecl PCIFindBoard(sgl_int8 *b, sgl_int8 *d, sgl_int16 v, sgl_int16 dev)
{ (void) b; (void) d; (void) v; (void) dev; return PCI_DEVICE_NOT_FOUND; }
int __cdecl PCIRead8(sgl_int8 b, sgl_int8 d, sgl_int16 r, sgl_int8 *x)
{ (void) b; (void) d; (void) r; *x = (sgl_int8) 0xff; return 0; }
int __cdecl PCIRead16(sgl_int8 b, sgl_int8 d, sgl_int16 r, sgl_int16 *x)
{ (void) b; (void) d; (void) r; *x = (sgl_int16) 0xffff; return 0; }
int __cdecl PCIRead32(sgl_int8 b, sgl_int8 d, sgl_int16 r, sgl_int32 *x)
{ (void) b; (void) d; (void) r; *x = (sgl_int32) 0xffffffff; return 0; }
int __cdecl PCIWrite8(sgl_int8 b, sgl_int8 d, sgl_int16 r, sgl_int8 x)
{ (void) b; (void) d; (void) r; (void) x; return 0; }
int __cdecl PCIWrite16(sgl_int8 b, sgl_int8 d, sgl_int16 r, sgl_int16 x)
{ (void) b; (void) d; (void) r; (void) x; return 0; }
int __cdecl PCIWrite32(sgl_int8 b, sgl_int8 d, sgl_int16 r, sgl_int32 x)
{ (void) b; (void) d; (void) r; (void) x; return 0; }

/* Stands in for MSVC's _CRT_INIT (see vsgl.h). */
BOOL WINAPI vsgl_crt_init(HINSTANCE h, DWORD reason, LPVOID reserved)
{
	(void) h; (void) reason; (void) reserved;
	return TRUE;
}
