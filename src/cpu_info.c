/*
 *
 * Gather CPU topology, cache sizes, frequencies, and supported
 * instruction‐set extensions on Windows and Linux.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>       /* for __cpuidex */
#include <processthreadsapi.h>
#else
#include <unistd.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <ctype.h>
#endif

/* L2 cache descriptor */
typedef struct {
	int l2cache_size; 				/* KiB */
	int shared_with_core_number;	/* how many logical cores share it */
} l2cache;

/* Core classification (for heterogeneous systems) */
typedef enum {
	CORE_TYPE_PERFORMANCE,
	CORE_TYPE_EFFICIENCY,
	CORE_TYPE_UNKNOWN
} CoreType;

/* One entry per physical core: ID, type, and its logical‐core indices */
typedef struct {
	int id;					/* platform‐specific core identifier */
	CoreType  type;			/* performance vs. efficiency */
	int* logical_ids;		/* array of logical‐core indices */
	int logical_count;		/* length of logical_ids */
} PhysicalCoreInfo;

/* Instruction‐set flags, split by vendor relevance */
typedef struct {
	/* common to both vendors */
	char SSE;
	char SSE2;
	char SSE3;
	char SSSE3;
	char SSE4_1;
	char SSE4_2;
	char AVX;
	/* Intel‐only */
	char POPCNT;
	char PCLMULQDQ;
	char AES;
	char FMA;		/* FMA3 */
	char F16C;
	char XSAVE;
	char OSXSAVE;
	char RDRAND;
	char RDSEED;
	char ADX;
	char MPX;
	char PREFETCHWT1;
	/* common leaf 7 */
	char AVX2;
	char BMI1;
	char BMI2;
	char AVX512F;
	char SHA;
	/* AMD‐only */
	char SSE4A;
	char XOP;
	char FMA4;
	char THREEDNOW_PLUS;
} CPU_Algorithms;

/* Aggregate CPU data */
typedef struct {
	char* cpu_name;				/* brand string */
	int logical_core_count;		/* total logical CPUs */
	int physical_core_count;	/* total physical cores */
	PhysicalCoreInfo* cores;	/* length = physical_core_count */
	int* l1size;				/* per‐logical L1 cache (KiB) */
	l2cache* l2size;			/* per‐logical L2 cache info */
	int* frequency;				/* per‐logical current MHz */
	int l3size;					/* shared L3 cache (KiB) */
	CPU_Algorithms algorithms;	/* instruction‐set flags */
} CPU_DATA;

/* Inline CPUID wrapper */
static inline void cpu_cpuid(int leaf, int subleaf, int regs[4]) {
#if defined(_MSC_VER)
	__cpuidex(regs, leaf, subleaf);
#elif defined(__i386__) || defined(__x86_64__)
	unsigned int a = (unsigned int)leaf, c = (unsigned int)subleaf;
	__asm__ volatile( "cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(a), "c"(c) );
#else
	regs[0] = regs[1] = regs[2] = regs[3] = 0;
#endif
}

/* Read the CPU brand string (leaves 0x80000002..4) */
static int get_cpu_brand(char** out_name) {
	char* brand = malloc(49);
	if (!brand) {
		return 203;
	}
	int regs[4];
	char* p = brand;

	for (int leaf = 0; leaf < 3; ++leaf) {
		cpu_cpuid(0x80000002 + leaf, 0, regs);
		memcpy(p, regs, 16);
		p += 16;
	}

	brand[48] = '\0';
	*out_name = brand;
	return 0;
}

/* Read vendor string into a 13‐byte buffer */
static void get_cpu_vendor(char vendor[13]) {
	int regs[4];
	cpu_cpuid(0, 0, regs);
	memcpy(vendor + 0, &regs[1], 4);
	memcpy(vendor + 4, &regs[3], 4);
	memcpy(vendor + 8, &regs[2], 4);
	vendor[12] = '\0';
}

/* Populate instruction‐set flags, gating AMD/Intel extras by vendor */
static void get_supported_algorithms(CPU_Algorithms* alg) {
	int regs[4];
	char vendor[13];
	memset(alg, 0, sizeof(*alg));

	get_cpu_vendor(vendor);
	int is_intel = (strcmp(vendor, "GenuineIntel") == 0);
	int is_amd = (strcmp(vendor, "AuthenticAMD") == 0);

	/* leaf 1: common SSE/AVX + Intel‐common extras */
	cpu_cpuid(1, 0, regs);
	alg->SSE = !!(regs[3] & (1 << 25));
	alg->SSE2 = !!(regs[3] & (1 << 26));
	alg->SSE3 = !!(regs[2] & (1 << 0));
	alg->SSSE3 = !!(regs[2] & (1 << 9));
	alg->SSE4_1 = !!(regs[2] & (1 << 19));
	alg->SSE4_2 = !!(regs[2] & (1 << 20));
	alg->AVX = !!(regs[2] & (1 << 28));

	if (is_intel) {
		alg->POPCNT = !!(regs[2] & (1 << 23));
		alg->PCLMULQDQ = !!(regs[2] & (1 << 1));
		alg->AES = !!(regs[2] & (1 << 25));
		alg->FMA = !!(regs[2] & (1 << 12));
		alg->F16C = !!(regs[2] & (1 << 29));
		alg->XSAVE = !!(regs[2] & (1 << 26));
		alg->OSXSAVE = !!(regs[2] & (1 << 27));
		alg->RDRAND = !!(regs[2] & (1 << 30));
	}

	/* leaf 7 subleaf 0: AVX2, BMI, AVX‐512, SHA, plus Intel‐only */
	cpu_cpuid(7, 0, regs);
	alg->AVX2 = !!(regs[1] & (1 << 5));
	alg->BMI1 = !!(regs[1] & (1 << 3));
	alg->BMI2 = !!(regs[1] & (1 << 8));
	alg->AVX512F = !!(regs[1] & (1 << 16));
	alg->SHA = !!(regs[2] & (1 << 29));

	if (is_intel) {
		alg->RDSEED = !!(regs[1] & (1 << 18));
		alg->ADX = !!(regs[1] & (1 << 19));
		alg->MPX = !!(regs[1] & (1 << 14));
		alg->PREFETCHWT1 = !!(regs[2] & (1 << 0));
	}

	/* AMD‐only leaf 0x80000001 */
	if (is_amd) {
		cpu_cpuid(0x80000001, 0, regs);
		alg->SSE4A = !!(regs[2] & (1 << 6));
		alg->XOP = !!(regs[2] & (1 << 11));
		alg->FMA4 = !!(regs[2] & (1 << 16));
		alg->THREEDNOW_PLUS = !!(regs[3] & (1U << 31));
	}
}

#if defined(_WIN32)
static int get_core_topology(CPU_DATA* data) {
	DWORD len = 0;
	BOOL ok;

	ok = GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)NULL, &len);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
		return 204;
	}

	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer =
		malloc(len);
	if (!buffer) {
		return 203;
	}

	ok = GetLogicalProcessorInformationEx(RelationProcessorCore, buffer, &len);
	if (!ok) {
		free(buffer);
		return 205;
	}

	char* ptr = (char*)buffer;
	DWORD offset = 0, core_count = 0;
	while (offset < len) {
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ptr;
		core_count++;
		offset += info->Size;
		ptr += info->Size;
	}

	data->physical_core_count = (int)core_count;
	data->cores = calloc(core_count, sizeof(*data->cores));
	if (!data->cores) {
		free(buffer);
		return 203;
	}

	ptr = (char*)buffer;
	offset = 0;
	for (DWORD i = 0; i < core_count; ++i) {
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ptr;

		data->cores[i].id = (int)i;
		data->cores[i].type = CORE_TYPE_UNKNOWN;

		KAFFINITY mask = info->Processor.GroupMask[0].Mask;
		int count = 0;
		for (int b = 0; b < 64; ++b) {
			if (mask & (1ULL << b)) {
				count++;
			}
		}

		data->cores[i].logical_count = count;
		data->cores[i].logical_ids = malloc(count * sizeof(int));

		int idx = 0;
		for (int b = 0; b < 64; ++b) {
			if (mask & (1ULL << b)) {
				data->cores[i].logical_ids[idx++] = b;
			}
		}

		offset += info->Size;
		ptr += info->Size;
	}

	free(buffer);
	return 0;
}
#else
static int get_core_topology(CPU_DATA* data) {
	int L = data->logical_core_count;
	int* keys = calloc(L, sizeof(int));
	PhysicalCoreInfo* temp = calloc(L, sizeof(*temp));
	if (!keys || !temp) return 203;

	int unique = 0;
	for (int cpu = 0; cpu < L; ++cpu) {
		char path_phy[128], path_core[128], buf[32];
		snprintf(path_phy, sizeof(path_phy), "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
		snprintf(path_core, sizeof(path_core), "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);

		FILE* f = fopen(path_phy, "r");
		int phy = 0;
		if (f) {
			fgets(buf, sizeof(buf), f);
			phy = atoi(buf);
			fclose(f);
		}

		f = fopen(path_core, "r");
		int core = 0;
		if (f) {
			fgets(buf, sizeof(buf), f);
			core = atoi(buf);
			fclose(f);
		}

		int key = (phy << 16) | (core & 0xFFFF);
		int idx = 0;
		while (idx < unique && keys[idx] != key) idx++;
		if (idx == unique) {
			keys[unique] = key;
			temp[unique].id = key;
			temp[unique].type = CORE_TYPE_UNKNOWN;
			temp[unique].logical_count = 0;
			unique++;
		}
		temp[idx].logical_count++;
	}

	data->physical_core_count = unique;
	data->cores = calloc(unique, sizeof(*data->cores));
	if (!data->cores) {
		free(keys);
		free(temp);
		return 203;
	}

	for (int i = 0; i < unique; ++i) {
		data->cores[i] = temp[i];
		data->cores[i].logical_ids =
			malloc(temp[i].logical_count * sizeof(int));
		data->cores[i].logical_count = 0;
	}

	for (int cpu = 0; cpu < L; ++cpu) {
		char path_phy[128], path_core[128], buf[32];
		snprintf(path_phy, sizeof(path_phy), "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
		snprintf(path_core, sizeof(path_core), "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);

		FILE* f = fopen(path_phy, "r");
		int phy = 0;
		if (f) {
			fgets(buf, sizeof(buf), f);
			phy = atoi(buf);
			fclose(f);
		}

		f = fopen(path_core, "r");
		int core = 0;
		if (f) {
			fgets(buf, sizeof(buf), f);
			core = atoi(buf);
			fclose(f);
		}

		int key = (phy << 16) | (core & 0xFFFF);
		int idx = 0;
		while (keys[idx] != key) idx++;

		data->cores[idx].logical_ids[data->cores[idx].logical_count++] = cpu;
	}

	free(keys);
	free(temp);
	return 0;
}
#endif

#if defined(_WIN32)
/*
 * Parse Windows cache relationships via
 * GetLogicalProcessorInformationEx(RelationCache, ...)
 */
static void populate_caches_and_freq(CPU_DATA* data) {
	/* first, zero‐out arrays */
	memset(data->l1size, 0, data->logical_core_count * sizeof(int));
	for (int i = 0; i < data->logical_core_count; ++i) {
		data->l2size[i].l2cache_size = 0;
		data->l2size[i].shared_with_core_number = 0;
	}
	data->l3size = 0;

	/* frequency via registry (~MHz) */
	for (int cpu = 0; cpu < data->logical_core_count; ++cpu) {
		char keypath[128];
		HKEY hKey;
		DWORD freq = 0, size = sizeof(freq);
		snprintf(keypath, sizeof(keypath), "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%d", cpu);
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keypath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
			RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&freq, &size);
			RegCloseKey(hKey);
		}
		data->frequency[cpu] = (int)freq;
	}

	/* now caches */
	DWORD len = 0;
	GetLogicalProcessorInformationEx(RelationCache, NULL, &len);
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer = malloc(len);
	GetLogicalProcessorInformationEx(RelationCache, buffer, &len);

	char* ptr = (char*)buffer;
	DWORD offset = 0;
	while (offset < len) {
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ptr;

		if (info->Relationship == RelationCache) {
			CACHE_RELATIONSHIP* c = &info->Cache;
			int level = c->Level;
			int sizeKB = (int)(c->CacheSize / 1024);
			KAFFINITY mask = c->GroupMask.Mask;

			/* count how many share */
			int shared = 0;
			for (int b = 0; b < 64; ++b) {
				if (mask & (1ULL << b)) {
					shared++;
				}
			}

			for (int b = 0; b < 64; ++b) {
				if (!(mask & (1ULL << b))) {
					continue;
				}
				if (level == 1) {
					data->l1size[b] = sizeKB;
				}
				else if (level == 2) {
					data->l2size[b].l2cache_size = sizeKB;
					data->l2size[b].shared_with_core_number = shared;
				}
				else if (level == 3) {
					data->l3size = sizeKB;
				}
			}
		}

		offset += info->Size;
		ptr += info->Size;
	}

	free(buffer);
}
#else
/*
 * Linux: freq from scaling_cur_freq, caches via sysfs cache/index*
 */
static void parse_shared_cpu_list(const char* str, int* out, int* out_count) {
	char* dup = strdup(str), * p = dup, * tok;
	int idx = 0;
	while ((tok = strsep(&p, ",")) != NULL) {
		int a, b;
		if (sscanf(tok, "%d-%d", &a, &b) == 2) {
			for (int i = a; i <= b; ++i) {
				out[idx++] = i;
			}
		}
		else if (sscanf(tok, "%d", &a) == 1) {
			out[idx++] = a;
		}
	}
	*out_count = idx;
	free(dup);
}

static void populate_caches_and_freq(CPU_DATA* data) {
	int L = data->logical_core_count;
	for (int i = 0; i < L; ++i) {
		data->l1size[i] = 0;
		data->l2size[i].l2cache_size = 0;
		data->l2size[i].shared_with_core_number = 0;
		data->frequency[i] = 0;
	}
	data->l3size = 0;

	/* frequency from /sys */
	for (int cpu = 0; cpu < L; ++cpu) {
		char path[128], buf[32];
		snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
		FILE* f = fopen(path, "r");
		if (f) {
			if (fgets(buf, sizeof(buf), f)) {
				/* file is in kHz */
				data->frequency[cpu] = atoi(buf) / 1000;
			}
			fclose(f);
		}
	}

	/* caches from cpu0 index */
	for (int idx = 0; idx < L; ++idx) {
		char dir[128], levelp[160], sizep[160], sharep[160], buf[64];
		snprintf(dir, sizeof(dir), "/sys/devices/system/cpu/cpu0/cache/index%d", idx);
		struct stat st;
		if (stat(dir, &st) != 0) {
			break;
		}

		snprintf(levelp, sizeof(levelp), "%s/level", dir);
		snprintf(sizep, sizeof(sizep), "%s/size", dir);
		snprintf(sharep, sizeof(sharep), "%s/shared_cpu_list", dir);

		FILE* f = fopen(levelp, "r");
		int level = 0;
		if (f) {
			fgets(buf, sizeof(buf), f);
			level = atoi(buf);
			fclose(f);
		}

		f = fopen(sizep, "r");
		int sizeKB = 0;
		if (f) {
			fgets(buf, sizeof(buf), f);
			sizeKB = atoi(buf);
			if (strchr(buf, 'M') || strchr(buf, 'm')) {
				sizeKB *= 1024;
			}
			fclose(f);
		}

		f = fopen(sharep, "r");
		char listbuf[64] = { 0 };
		if (f) {
			fgets(listbuf, sizeof(listbuf), f);
			fclose(f);
		}

		int cpus[256], cpuc = 0;
		parse_shared_cpu_list(listbuf, cpus, &cpuc);

		/* assign */
		for (int i = 0; i < cpuc; ++i) {
			int c = cpus[i];
			if (level == 1) {
				data->l1size[c] = sizeKB;
			}
			else if (level == 2) {
				data->l2size[c].l2cache_size = sizeKB;
				data->l2size[c].shared_with_core_number = cpuc;
			}
			else if (level == 3) {
				data->l3size = sizeKB;
			}
		}
	}
}
#endif

/* Top‐level entry: fills CPU_DATA fields */
#if defined(_WIN32)
__declspec(dllexport)
#endif
int get_cpu_data(CPU_DATA* data) {
	if (!data) {
		return 201;
	}

	/* brand string */
	int rc = get_cpu_brand(&data->cpu_name);
	if (rc != 200) {
		return rc;
	}

	/* logical cores */
#if defined(_WIN32)
	data->logical_core_count = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
	data->logical_core_count = get_nprocs();
#endif

	/* allocate arrays */
	data->l1size = calloc(data->logical_core_count, sizeof(int));
	data->l2size = calloc(data->logical_core_count, sizeof(l2cache));
	data->frequency = calloc(data->logical_core_count, sizeof(int));
	data->l3size = 0;
	if (!data->l1size || !data->l2size || !data->frequency) {
		return 203;
	}

	/* fill caches & frequency */
	populate_caches_and_freq(data);

	/* instruction‐set flags */
	get_supported_algorithms(&data->algorithms);

	/* physical‐core topology */
	rc = get_core_topology(data);
	if (rc != 200) {
		return rc;
	}

	return 0;
}
