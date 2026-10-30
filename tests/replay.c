/*
 * replay.c - compare the wrapper with a real m3D on recorded frames.
 *
 * Reads a pcx2session result folder from the PCX2 tools (report.txt, the
 * frame_NNNNNN.bin dumps it names, and NAME-card.raw for each case: what the
 * card drew). For every case it applies the same register overrides, renders
 * the dump through sgl.dll's own render path (pvr_test_render, i.e. the
 * VxD stand-in's SCHEDULE_RENDER and frame write-out), and counts pixels
 * that differ from the card. Run with sgl.dll next to it:
 *
 *     replay.exe SESSIONDIR
 *
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef int (*PFN_RENDER)(const uint32_t *, const uint32_t *, uint32_t, uint32_t,
                          const uint32_t *, void *);

static unsigned char *load(const char *path, long *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *d;
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc(*len);
	if (d && fread(d, 1, *len, f) != (size_t) *len) {
		free(d);
		d = NULL;
	}
	fclose(f);
	return d;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[MAX_PATH], line[1024], name[128] = "", dump[MAX_PATH] = "";
	int nset = 0, set_idx[16], cases = 0, failed = 0, unmodelled = 0, skipped = 0;
	uint32_t set_val[16];
	PFN_RENDER render;
	HMODULE h;
	FILE *rep;

	h = LoadLibraryA("sgl.dll");
	render = h ? (PFN_RENDER) GetProcAddress(h, "pvr_test_render") : NULL;
	if (!render) {
		printf("sgl.dll with pvr_test_render not found\n");
		return 1;
	}
	snprintf(path, sizeof path, "%s/report.txt", dir);
	if (!(rep = fopen(path, "r"))) {
		printf("no %s\n", path);
		return 1;
	}
	while (fgets(line, sizeof line, rep)) {
		char *p;
		unsigned stride;
		if (!strncmp(line, "== ", 3)) {
			/* "== NAME: /path/frame_N.bin [idx]=val ..." */
			char *colon = strchr(line, ':'), *slash, *sp;
			nset = 0;
			if (!colon)
				continue;
			*colon = 0;
			snprintf(name, sizeof name, "%s", line + 3);
			p = colon + 2;
			sp = strpbrk(p, " \n");
			if (sp)
				*sp++ = 0;
			slash = strrchr(p, '/');
			snprintf(dump, sizeof dump, "%s/%s", dir, slash ? slash + 1 : p);
			while (sp && (p = strchr(sp, '[')) && nset < 16) {
				unsigned idx, val;
				if (sscanf(p, "[%x]=%x", &idx, &val) == 2) {
					set_idx[nset] = (int) idx;
					set_val[nset++] = val;
				}
				sp = p + 1;
			}
			continue;
		}
		if (!name[0] || !(p = strstr(line, "LSTRIDE ")) || sscanf(p, "LSTRIDE %u", &stride) != 1)
			continue;
		{
			/* The PACKMODE line: render this case and compare. */
			long dlen, clen;
			unsigned char *d = load(dump, &dlen), *card, *fb;
			uint32_t regs[0x400], page, pages, objb, pm;
			int bpp, H, x, y, diff = 0, i;

			snprintf(path, sizeof path, "%s/%s-card.raw", dir, name);
			card = load(path, &clen);
			if (!d || !card || memcmp(d, "PCXDUMP1", 8)) {
				printf("%-22s missing dump or card image, skipped\n", name);
				skipped++;
				free(d);
				free(card);
				name[0] = 0;
				continue;
			}
			page = ((uint32_t *) d)[5];
			pages = ((uint32_t *) d)[6];
			objb = ((uint32_t *) d)[7];
			memcpy(regs, d + 64, sizeof regs);
			for (i = 0; i < nset; i++)
				regs[set_idx[i]] = set_val[i];
			regs[0x12] = stride;
			pm = regs[0x10];
			bpp = ((pm & 3) == 0) ? 4 : ((pm & 3) == 1) ? 3 : 2;
			H = (int) (clen / stride);
			fb = malloc(clen);
			memset(fb, 0xa5, clen); /* as pcx2play fills its frame buffer */
			render(regs, (const uint32_t *) (d + 64 + 4096), page * pages, page,
			       (const uint32_t *) (d + 64 + 4096 + page * pages + objb), fb);
			for (y = 0; y < H; y++)
				for (x = 0; x < (int) (stride / bpp); x++)
					if (memcmp(fb + y * stride + x * bpp, card + y * stride + x * bpp, bpp))
						diff++;
			{
				/* PREC_BASE changed by the session: the card scrambles its
				   output in a way nothing models and no driver uses. */
				int prec = 0;
				for (i = 0; i < nset; i++)
					if (set_idx[i] == 0x0b && set_val[i] != 0)
						prec = 1;
				printf("%-22s %s  %ux%d  PACKMODE %02x: %d pixels differ from the card%s\n", name,
				       strrchr(dump, '/') ? strrchr(dump, '/') + 1 : dump, stride / bpp, H, pm, diff,
				       prec ? " (PREC_BASE changed: not modelled, not counted)" : "");
				if (prec)
					unmodelled++;
				else {
					cases++;
					if (diff)
						failed++;
				}
			}
			free(d);
			free(card);
			free(fb);
			name[0] = 0;
		}
	}
	fclose(rep);
	if (unmodelled || skipped)
		printf("(%d PREC_BASE cases not counted, %d skipped)\n", unmodelled, skipped);
	printf("%d cases, %d with differences\n", cases, failed);
	return 0;
}
