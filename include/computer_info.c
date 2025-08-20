#ifndef CPU_DATA_H
#define CPU_DATA_H

#if defined(_WIN32)
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------- Struct Definitions --------------------


/* L2 cache descriptor */
	typedef struct {
		int l2cache_size;            /* KiB */
		int shared_with_core_number; /* how many logical cores share it */
	} l2cache;

	/* Core classification (for heterogeneous systems) */
	typedef enum {
		CORE_TYPE_PERFORMANCE,
		CORE_TYPE_EFFICIENCY,
		CORE_TYPE_UNKNOWN
	} CoreType;

	/* One entry per physical core: ID, type, and its logical‐core indices */
	typedef struct {
		int       id;               /* platform‐specific core identifier */
		CoreType  type;             /* performance vs. efficiency */
		int* logical_ids;      /* array of logical‐core indices */
		int       logical_count;    /* length of logical_ids */
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
		char FMA;          /* FMA3 */
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
		char* cpu_name;                   /* brand string */
		int                logical_core_count;         /* total logical CPUs */
		int                physical_core_count;        /* total physical cores */
		PhysicalCoreInfo* cores;                      /* length = physical_core_count */

		int* l1size;                     /* per‐logical L1 cache (KiB) */
		l2cache* l2size;                     /* per‐logical L2 cache info */
		int* frequency;                  /* per‐logical current MHz */
		int                l3size;                     /* shared L3 cache (KiB) */

		CPU_Algorithms     algorithms;                 /* instruction‐set flags */
	} CPU_DATA;
	// -------------------- Exported Function --------------------

	DLL_EXPORT int get_cpu_data(CPU_DATA* data);

#ifdef __cplusplus
}
#endif

#endif // CPU_DATA_H


/*
typedef int (*get_cpu_data_fn)(CPU_DATA*);
Allocate and zero out a CPU_DATA struct before calling

Free the internal arrays (l1size, frequency, l2size) after use


0		Success — no errors
201		Null pointer passed to get_cpu_data
202		Failed to open system info (Windows or Linux)
203		Memory allocation failure
204–207	Windows-specific API failures
208		Allocation failure for internal arrays

*/
