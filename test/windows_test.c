#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CPU_Info.h"

typedef int (*get_cpu_data_fn)(CPU_DATA*);

static const char* core_type_to_string(CoreType t) {
	switch (t) {
		case CORE_TYPE_PERFORMANCE: return "Performance";
		case CORE_TYPE_EFFICIENCY:  return "Efficiency";
		default:                    return "Unknown";
	}
}

int main(void) {
	HMODULE lib = LoadLibraryA("CPU-Info.dll");
	if (!lib) {
		fprintf(stderr, "Failed to load DLL\n");
		return 1;
	}

	get_cpu_data_fn get_cpu_data = (get_cpu_data_fn)GetProcAddress(lib, "get_cpu_data");
	if (!get_cpu_data) {
		fprintf(stderr, "Failed to find get_cpu_data\n");
		FreeLibrary(lib);
		return 2;
	}

	CPU_DATA data = { 0 };
	int rc = get_cpu_data(&data);
	if (rc != 200) {
		fprintf(stderr, "get_cpu_data failed with code %d\n", rc);
		FreeLibrary(lib);
		return 3;
	}

	FILE* f = fopen("CPU_Info.txt", "w");
	if (!f) {
		fprintf(stderr, "Failed to open output file\n");
		FreeLibrary(lib);
		return 4;
	}

	// Basic summary
	fprintf(f, "CPU Brand String: %s\n", data.cpu_name);
	fprintf(f, "Physical Cores   : %d\n", data.physical_core_count);
	fprintf(f, "Logical Cores    : %d\n", data.logical_core_count);
	fprintf(f, "L3 Cache         : %d KB\n\n", data.l3size);

	// Physical core topology
	fprintf(f, "Physical Core Topology:\n");
	for (int i = 0; i < data.physical_core_count; ++i) {
		PhysicalCoreInfo* pc = &data.cores[i];
		fprintf(f, "  Core %d (%s): %d logical siblings: ",
				pc->id,
				core_type_to_string(pc->type),
				pc->logical_count);
		for (int j = 0; j < pc->logical_count; ++j) {
			fprintf(f, "%d", pc->logical_ids[j]);
			if (j + 1 < pc->logical_count) fprintf(f, ", ");
		}
		fprintf(f, "\n");
	}
	fprintf(f, "\n");

	// Per‐logical‐core details
	fprintf(f, "Per‐Logical‐Core Details:\n");
	for (int i = 0; i < data.logical_core_count; ++i) {
		fprintf(f, "  Logical Core %2d:\n", i);
		fprintf(f, "    Frequency : %4d MHz\n", data.frequency[i]);
		fprintf(f, "    L1 Cache  : %4d KB\n", data.l1size[i]);
		fprintf(f, "    L2 Cache  : %4d KB (shared with %d cores)\n",
				data.l2size[i].l2cache_size,
				data.l2size[i].shared_with_core_number);
	}
	fprintf(f, "\n");

	// Instruction‐set extensions
	fprintf(f, "Instruction‐set Extensions:\n");
#define PRINT_FLAG(field, name) \
        do { if (data.algorithms.field) fprintf(f, "  %s\n", name); } while (0)

	// common
	PRINT_FLAG(SSE, "SSE");
	PRINT_FLAG(SSE2, "SSE2");
	PRINT_FLAG(SSE3, "SSE3");
	PRINT_FLAG(SSSE3, "SSSE3");
	PRINT_FLAG(SSE4_1, "SSE4.1");
	PRINT_FLAG(SSE4_2, "SSE4.2");
	PRINT_FLAG(AVX, "AVX");

	// intel‐only
	PRINT_FLAG(POPCNT, "POPCNT");
	PRINT_FLAG(PCLMULQDQ, "PCLMULQDQ");
	PRINT_FLAG(AES, "AES");
	PRINT_FLAG(FMA, "FMA3");
	PRINT_FLAG(F16C, "F16C");
	PRINT_FLAG(XSAVE, "XSAVE");
	PRINT_FLAG(OSXSAVE, "OSXSAVE");
	PRINT_FLAG(RDRAND, "RDRAND");
	PRINT_FLAG(RDSEED, "RDSEED");
	PRINT_FLAG(ADX, "ADX");
	PRINT_FLAG(MPX, "MPX");
	PRINT_FLAG(PREFETCHWT1, "PREFETCHWT1");

	// leaf7/common
	PRINT_FLAG(AVX2, "AVX2");
	PRINT_FLAG(BMI1, "BMI1");
	PRINT_FLAG(BMI2, "BMI2");
	PRINT_FLAG(AVX512F, "AVX512F");
	PRINT_FLAG(SHA, "SHA");

	// amd‐only
	PRINT_FLAG(SSE4A, "SSE4A");
	PRINT_FLAG(XOP, "XOP");
	PRINT_FLAG(FMA4, "FMA4");
	PRINT_FLAG(THREEDNOW_PLUS, "3DNow+");

#undef PRINT_FLAG

	fclose(f);

	// cleanup
	free(data.cpu_name);
	free(data.frequency);
	free(data.l1size);
	free(data.l2size);
	for (int i = 0; i < data.physical_core_count; ++i) {
		free(data.cores[i].logical_ids);
	}
	free(data.cores);

	FreeLibrary(lib);
	return 0;
}
