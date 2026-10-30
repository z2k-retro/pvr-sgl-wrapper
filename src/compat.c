/*
 * pvr-sgl-wrapper: settings games read from sglhw.ini themselves.
 *
 * The PowerVR driver installed sglhw.ini in the Windows folder, and some
 * games read it directly rather than through SGL. POD (podpvrx.exe) reads
 * [Default] SW_XRegionSize and SW_YRegionSize with a default of 0 and
 * divides the screen size by them, so without the file it crashes with a
 * divide by zero before the first race.
 *
 * GetPrivateProfileIntA is redirected in the game's import table. A read
 * of "sglhw.ini" looks in sglhw.ini next to sgl.dll first, then the
 * Windows folder's, and then falls back to the driver's own defaults for
 * the keys it knows.
 *
 * MIT licence, as the rest of the wrapper.
 */
#include <windows.h>
#include <string.h>

void pvr_log(const char *fmt, ...);
const char *pvr_dll_dir(void);

typedef UINT (WINAPI *gppi_fn)(LPCSTR, LPCSTR, INT, LPCSTR);
static gppi_fn real_gppi;

#define NOT_FOUND ((UINT) 0x7ffe1234)

static const struct {
	const char *key;
	UINT value;
} defaults[] = {
	/* win32/hwdevice.c X_REGION_SIZE / Y_REGION_SIZE */
	{ "SW_XRegionSize", 32 },
	{ "SW_YRegionSize", 32 },
};

static int is_sglhw(LPCSTR file)
{
	return file && !strchr(file, '\\') && !strchr(file, '/') && !lstrcmpiA(file, "sglhw.ini");
}

static UINT WINAPI hook_gppi(LPCSTR section, LPCSTR key, INT def, LPCSTR file)
{
	char path[MAX_PATH + 16];
	UINT v;
	unsigned i;

	if (!is_sglhw(file) || !key)
		return real_gppi(section, key, def, file);

	lstrcpynA(path, pvr_dll_dir(), MAX_PATH);
	lstrcatA(path, "sglhw.ini");
	v = real_gppi(section, key, (INT) NOT_FOUND, path);
	if (v != NOT_FOUND)
		return v;
	v = real_gppi(section, key, (INT) NOT_FOUND, file);
	if (v != NOT_FOUND)
		return v;
	for (i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
		if (!lstrcmpiA(key, defaults[i].key) && def == 0) {
			pvr_log("compat: sglhw.ini [%s] %s not set, using %u\n",
			        section ? section : "", key, defaults[i].value);
			return defaults[i].value;
		}
	return (UINT) def;
}

/* Point the module's GetPrivateProfileIntA import at the hook. */
static int patch_module(HMODULE m)
{
	BYTE *base = (BYTE *) m;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *) base;
	IMAGE_NT_HEADERS *nt;
	IMAGE_DATA_DIRECTORY *dir;
	IMAGE_IMPORT_DESCRIPTOR *imp;
	int n = 0;

	if (!m || dos->e_magic != IMAGE_DOS_SIGNATURE)
		return 0;
	nt = (IMAGE_NT_HEADERS *) (base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return 0;
	dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir->VirtualAddress)
		return 0;
	for (imp = (IMAGE_IMPORT_DESCRIPTOR *) (base + dir->VirtualAddress); imp->Name; imp++) {
		IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *) (base + imp->FirstThunk);
		IMAGE_THUNK_DATA *names = imp->OriginalFirstThunk ?
			(IMAGE_THUNK_DATA *) (base + imp->OriginalFirstThunk) : NULL;
		for (; iat->u1.Function; iat++, names = names ? names + 1 : NULL) {
			int match = (void *) iat->u1.Function == (void *) real_gppi;
			if (!match && names && !IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
				IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *) (base + names->u1.AddressOfData);
				match = !strcmp((const char *) ibn->Name, "GetPrivateProfileIntA");
			}
			if (match) {
				DWORD old;
				if (VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, PAGE_READWRITE, &old)) {
					iat->u1.Function = (DWORD_PTR) hook_gppi;
					VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, old, &old);
					n++;
				}
			}
		}
	}
	return n;
}

void compat_init(void)
{
	HMODULE k32 = GetModuleHandleA("kernel32.dll");

	if (!k32 || real_gppi)
		return;
	real_gppi = (gppi_fn) GetProcAddress(k32, "GetPrivateProfileIntA");
	if (!real_gppi)
		return;
	if (patch_module(GetModuleHandleA(NULL)))
		pvr_log("compat: sglhw.ini reads by the game are handled\n");
}
