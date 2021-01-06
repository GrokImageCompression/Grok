/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
#include "CPUArch.h"
#include <algorithm>
#include <limits>

namespace grk {

/* <summary>                             */
/* Determine maximum computed resolution level for inverse wavelet transform */
/* </summary>                            */
uint32_t max_resolution(Resolution *GRK_RESTRICT r, uint32_t i) {
	uint32_t mr = 0;
	while (--i) {
		++r;
		uint32_t w;
		if (mr < (w = r->x1 - r->x0))
			mr = w;
		if (mr < (w = r->y1 - r->y0))
			mr = w;
	}
	return mr;
}

template <typename T, typename S> struct decompress_job{
	decompress_job( S data,
				T * GRK_RESTRICT LL,
				uint32_t sLL,
				T * GRK_RESTRICT HL,
				uint32_t sHL,
				T * GRK_RESTRICT LH,
				uint32_t sLH,
				T * GRK_RESTRICT HH,
				uint32_t sHH,
				T * GRK_RESTRICT destination,
				uint32_t strideDestination,
				uint32_t min_j,
				uint32_t max_j) : data(data),
								bandLL(LL),
								strideLL(sLL),
								bandHL(HL),
								strideHL(sHL),
								bandLH(LH),
								strideLH(sLH),
								bandHH(HH),
								strideHH(sHH),
								dest(destination),
								strideDest(strideDestination),
								min_j(min_j),
								max_j(max_j)
	{}
	decompress_job( S data,
				uint32_t min_j,
				uint32_t max_j) :
					decompress_job(data,
							nullptr,
							0,
							nullptr,
							0,
							nullptr,
							0,
							nullptr,
							0,
							nullptr,
							0,
							min_j,
							max_j)
	{}
    S data;
    T * GRK_RESTRICT bandLL;
    uint32_t strideLL;
    T * GRK_RESTRICT bandHL;
    uint32_t strideHL;
    T * GRK_RESTRICT bandLH;
    uint32_t strideLH;
    T * GRK_RESTRICT bandHH;
    uint32_t strideHH;
    T * GRK_RESTRICT dest;
    uint32_t strideDest;

    uint32_t min_j;
    uint32_t max_j;
} ;


/** Number of columns that we can process in parallel in the vertical pass */
#define PLL_COLS_53     (2*VREG_INT_COUNT)
template <typename T> struct dwt_data {
	dwt_data() : allocatedMem(nullptr),
				 mem(nullptr),
				 memLow(nullptr),
				 memHigh(nullptr),
		         dn(0),
				 sn(0),
				 cas(0),
				 win_l_0(0),
				 win_l_1(0),
				 win_h_0(0),
				 win_h_1(0)
	{}

	dwt_data(const dwt_data& rhs) : allocatedMem(nullptr),
									mem(nullptr),
									memLow(nullptr),
									memHigh(nullptr),
									dn ( rhs.dn),
									sn ( rhs.sn),
									cas ( rhs.cas),
									win_l_0 ( rhs.win_l_0),
									win_l_1 ( rhs.win_l_1),
									win_h_0 ( rhs.win_h_0),
									win_h_1 ( rhs.win_h_1)
	{}

	bool alloc(size_t len) {
		return alloc(len,0);
	}

	bool alloc(size_t len, size_t padding) {
		release();

	    /* overflow check */
	    if (len > (SIZE_MAX / sizeof(T))) {
	        GRK_ERROR("data size overflow");
	        return false;
	    }
	    len = (len +  2 * padding) * sizeof(T);
	    allocatedMem = (T*)grk_aligned_malloc(len);
	    if (!allocatedMem){
	        GRK_ERROR("Failed to allocate %d bytes", len);
	        return false;
	    }
#ifdef DEBUG_SPARSE
		for (uint32_t i = 0; i < len / sizeof(T); ++i)
			allocatedMem[i] = T(debugFill);
#endif
	    mem = allocatedMem + padding;
		return (allocatedMem != nullptr) ? true : false;
	}
	void release(){
		grk_aligned_free(allocatedMem);
		allocatedMem = nullptr;
		mem = nullptr;
		memLow = nullptr;
		memHigh = nullptr;
	}
	T* allocatedMem;
    T* mem;
    T* memLow;
    T* memHigh;
    uint32_t dn;   /* number of elements in high pass band */
    uint32_t sn;   /* number of elements in low pass band */
    uint32_t cas;  /* 0 = start on even coord, 1 = start on odd coord */
    uint32_t      win_l_0; /* start coord in low pass band */
    uint32_t      win_l_1; /* end coord in low pass band */
    uint32_t      win_h_0; /* start coord in high pass band */
    uint32_t      win_h_1; /* end coord in high pass band */
};

struct Params97{
	Params97() :dataPrev(nullptr),
					data(nullptr),
					absoluteStart(0),
					len(0),
					lenMax(0)
	{}
	vec4f* dataPrev;
	vec4f* data;
	uint32_t absoluteStart;
	uint32_t len;
	uint32_t lenMax;
};

static Params97 makeParams97(dwt_data<vec4f>* dwt, bool low, bool step1);

static const float dwt_alpha =  1.586134342f; /*  12994 */
static const float dwt_beta  =  0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */

static const float K      = 1.230174105f; /*  10078 */
static const float c13318 = 1.625732422f;

static void  decompress_h_cas0_53(int32_t* buf,
                               int32_t* bandL, /* even */
								const uint32_t wL,
							   int32_t* bandH,
								const uint32_t wH,
								int32_t *dest){ /* odd */
	const uint32_t total_width = wL + wH;
    assert(total_width > 1);

    /* Improved version of the TWO_PASS_VERSION: */
    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */
    int32_t s1n = bandL[0];
    int32_t d1n = bandH[0];
    int32_t s0n = s1n - ((d1n + 1) >> 1);

    uint32_t i = 0;

    if (total_width > 2) {
		for (uint32_t j = 1; i < (total_width - 3); i += 2, j++) {
			int32_t d1c = d1n;
			int32_t s0c = s0n;

			s1n = bandL[j];
			d1n = bandH[j];
			s0n = s1n - ((d1c + d1n + 2) >> 2);
			buf[i  ] = s0c;
			buf[i + 1] = d1c + ((s0c + s0n) >> 1);
		}
    }

    buf[i] = s0n;
    if (total_width & 1) {
        buf[total_width - 1] = bandL[(total_width - 1) >> 1] - ((d1n + 1) >> 1);
        buf[total_width - 2] = d1n + ((s0n + buf[total_width - 1]) >> 1);
    } else {
        buf[total_width - 1] = d1n + s0n;
    }
    memcpy(dest, buf, total_width * sizeof(int32_t));
}

static void  decompress_h_cas1_53(int32_t* buf,
							   int32_t* bandL, /* odd */
                               const uint32_t wL,
							   int32_t* bandH,
                               const uint32_t wH,
							   int32_t *dest){ /* even */
	const uint32_t total_width = wL + wH;
    assert(total_width > 2);

    /* Improved version of the TWO_PASS_VERSION:
       Performs lifting in one single iteration. Saves memory
       accesses and explicit interleaving. */
    int32_t s1 = bandH[1];
    int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
    buf[0] = bandH[0] + dc;
    uint32_t i, j;
    for (i = 1, j = 1; i < (total_width - 2 - !(total_width & 1)); i += 2, j++) {
    	int32_t s2 = bandH[j + 1];
    	int32_t dn = bandL[j] - ((s1 + s2 + 2) >> 2);

        buf[i  ] = dc;
        buf[i + 1] = s1 + ((dn + dc) >> 1);
        dc = dn;
        s1 = s2;
    }

    buf[i] = dc;

    if (!(total_width & 1)) {
    	int32_t dn = bandL[total_width / 2 - 1] - ((s1 + 1) >> 1);
        buf[total_width - 2] = s1 + ((dn + dc) >> 1);
        buf[total_width - 1] = dn;
    } else {
        buf[total_width - 1] = s1 + dc;
    }
    memcpy(dest, buf, total_width * sizeof(int32_t));
}

#if (defined(__SSE2__) || defined(__AVX2__))

static
void decompress_v_final_memcpy_53( const int32_t* buf,
							const uint32_t height,
							int32_t* dest,
							const size_t strideDest){
	for (uint32_t i = 0; i < height; ++i) {
        /* A memcpy(&tiledp_col[i * stride + 0],
                    &tmp[PARALLEL_COLS_53 * i + 0],
                    PARALLEL_COLS_53 * sizeof(int32_t))
           would do but would be a tiny bit slower.
           We can take here advantage of our knowledge of alignment */
        STOREU(&dest[(size_t)i * strideDest + 0],              LOAD(&buf[PLL_COLS_53 * i + 0]));
        STOREU(&dest[(size_t)i * strideDest + VREG_INT_COUNT], LOAD(&buf[PLL_COLS_53 * i + VREG_INT_COUNT]));
    }
}

/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on even coordinate */
static void decompress_v_cas0_mcols_SSE2_OR_AVX2_53(int32_t* buf,
												int32_t* bandL, /* even */
												const uint32_t hL,
												const size_t strideL,
												int32_t *bandH, /* odd */
												const uint32_t hH,
												const size_t strideH,
												int32_t *dest,
												const uint32_t strideDest){
    const VREG two = LOAD_CST(2);

	const uint32_t total_height = hL + hH;
    assert(total_height > 1);

    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in tmp can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)buf % (sizeof(int32_t) * VREG_INT_COUNT) == 0);

    VREG s1n_0 = LOADU(bandL + 0);
    VREG s1n_1 = LOADU(bandL + VREG_INT_COUNT);
    VREG d1n_0 = LOADU(bandH);
    VREG d1n_1 = LOADU(bandH + VREG_INT_COUNT);

    /* s0n = s1n - ((d1n + 1) >> 1); <==> */
    /* s0n = s1n - ((d1n + d1n + 2) >> 2); */
    VREG s0n_0 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
    VREG s0n_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));

    uint32_t i = 0;
    if (total_height > 3) {
        uint32_t j;
		for (i = 0, j = 1; i < (total_height - 3); i += 2, j++) {
			VREG d1c_0 = d1n_0;
			VREG s0c_0 = s0n_0;
			VREG d1c_1 = d1n_1;
			VREG s0c_1 = s0n_1;

			s1n_0 = LOADU(bandL + j * strideL);
			s1n_1 = LOADU(bandL + j * strideL + VREG_INT_COUNT);
			d1n_0 = LOADU(bandH + j * strideH);
			d1n_1 = LOADU(bandH + j * strideH + VREG_INT_COUNT);

			/*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
			s0n_0 = SUB(s1n_0, SAR(ADD3(d1c_0, d1n_0, two), 2));
			s0n_1 = SUB(s1n_1, SAR(ADD3(d1c_1, d1n_1, two), 2));

			STORE(buf + PLL_COLS_53 * (i + 0), s0c_0);
			STORE(buf + PLL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0c_1);

			/* d1c + ((s0c + s0n) >> 1) */
			STORE(buf + PLL_COLS_53 * (i + 1) + 0,              ADD(d1c_0, SAR(ADD(s0c_0, s0n_0), 1)));
			STORE(buf + PLL_COLS_53 * (i + 1) + VREG_INT_COUNT, ADD(d1c_1, SAR(ADD(s0c_1, s0n_1), 1)));
		}
    }

    STORE(buf + PLL_COLS_53 * (i + 0) + 0, s0n_0);
    STORE(buf + PLL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0n_1);

    if (total_height & 1) {
        VREG tmp_len_minus_1;
        s1n_0 = LOADU(bandL + (size_t)((total_height - 1) / 2) * strideL);
        /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
        tmp_len_minus_1 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
        STORE(buf + PLL_COLS_53 * (total_height - 1), tmp_len_minus_1);
        /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
        STORE(buf + PLL_COLS_53 * (total_height - 2), ADD(d1n_0, SAR(ADD(s0n_0, tmp_len_minus_1), 1)));

        s1n_1 = LOADU(bandL + (size_t)((total_height - 1) / 2) * strideL + VREG_INT_COUNT);
        /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
        tmp_len_minus_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT, tmp_len_minus_1);
        /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
        STORE(buf + PLL_COLS_53 * (total_height - 2) + VREG_INT_COUNT, ADD(d1n_1, SAR(ADD(s0n_1, tmp_len_minus_1), 1)));

    } else {
        STORE(buf + PLL_COLS_53 * (total_height - 1) + 0,              ADD(d1n_0, s0n_0));
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT, ADD(d1n_1, s0n_1));
    }
    decompress_v_final_memcpy_53(buf,total_height, dest, strideDest);
}


/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on odd coordinate */
static void decompress_v_cas1_mcols_SSE2_OR_AVX2_53(int32_t* buf,
												int32_t* bandL,
												const uint32_t hL,
												const uint32_t strideL,
												int32_t *bandH,
												const uint32_t hH,
												const uint32_t strideH,
												int32_t *dest,
												const uint32_t strideDest){
    const VREG two = LOAD_CST(2);

    const uint32_t total_height = hL + hH;
    assert(total_height > 2);
    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in buf can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)buf % (sizeof(int32_t) * VREG_INT_COUNT) == 0);

    const int32_t* in_even = bandH;
    const int32_t* in_odd = bandL;
    VREG s1_0 = LOADU(in_even + strideH);
    /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
    VREG dc_0 = SUB(LOADU(in_odd + 0), SAR(ADD3(LOADU(in_even + 0), s1_0, two), 2));
    STORE(buf + PLL_COLS_53 * 0, ADD(LOADU(in_even + 0), dc_0));

    VREG s1_1 = LOADU(in_even + strideH + VREG_INT_COUNT);
    /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
    VREG dc_1 = SUB(LOADU(in_odd + VREG_INT_COUNT), SAR(ADD3(LOADU(in_even + VREG_INT_COUNT), s1_1, two), 2));
    STORE(buf + PLL_COLS_53 * 0 + VREG_INT_COUNT,   ADD(LOADU(in_even + VREG_INT_COUNT), dc_1));

    uint32_t i;
    size_t j;
    for (i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++) {

    	VREG s2_0 = LOADU(in_even + (j + 1) * strideH);
    	VREG s2_1 = LOADU(in_even + (j + 1) * strideH + VREG_INT_COUNT);

        /* dn = in_odd[j * stride] - ((s1 + s2 + 2) >> 2); */
    	VREG dn_0 = SUB(LOADU(in_odd + j * strideL),                 SAR(ADD3(s1_0, s2_0, two), 2));
    	VREG dn_1 = SUB(LOADU(in_odd + j * strideL + VREG_INT_COUNT),SAR(ADD3(s1_1, s2_1, two), 2));

        STORE(buf + PLL_COLS_53 * i, dc_0);
        STORE(buf + PLL_COLS_53 * i + VREG_INT_COUNT, dc_1);

        /* buf[i + 1] = s1 + ((dn + dc) >> 1); */
        STORE(buf + PLL_COLS_53 * (i + 1) + 0,             ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
        STORE(buf + PLL_COLS_53 * (i + 1) + VREG_INT_COUNT,ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

        dc_0 = dn_0;
        s1_0 = s2_0;
        dc_1 = dn_1;
        s1_1 = s2_1;
    }
    STORE(buf + PLL_COLS_53 * i, dc_0);
    STORE(buf + PLL_COLS_53 * i + VREG_INT_COUNT, dc_1);

    if (!(total_height & 1)) {
        /*dn = in_odd[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
    	VREG dn_0 = SUB(LOADU(in_odd + (size_t)(total_height / 2 - 1) * strideL),SAR(ADD3(s1_0, s1_0, two), 2));
    	VREG dn_1 = SUB(LOADU(in_odd + (size_t)(total_height / 2 - 1) * strideL + VREG_INT_COUNT), SAR(ADD3(s1_1, s1_1, two), 2));

        /* buf[len - 2] = s1 + ((dn + dc) >> 1); */
        STORE(buf + PLL_COLS_53 * (total_height - 2) + 0, ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
        STORE(buf + PLL_COLS_53 * (total_height - 2) + VREG_INT_COUNT, ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

        STORE(buf + PLL_COLS_53 * (total_height - 1) + 0, dn_0);
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT, dn_1);
    } else {
        STORE(buf + PLL_COLS_53 * (total_height - 1) + 0, ADD(s1_0, dc_0));
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT,ADD(s1_1, dc_1));
    }
    decompress_v_final_memcpy_53(buf, total_height, dest, strideDest);
}

#undef VREG
#undef LOAD_CST
#undef LOADU
#undef LOAD
#undef STORE
#undef STOREU
#undef ADD
#undef ADD3
#undef SUB
#undef SAR

#endif /* (defined(__SSE2__) || defined(__AVX2__)) */

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
static void decompress_v_cas0_53(int32_t* buf,
                             int32_t* bandL,
							 const uint32_t hL,
							 const uint32_t strideL,
							 int32_t *bandH,
							 const uint32_t hH,
                             const uint32_t strideH,
							 int32_t *dest,
							 const uint32_t strideDest){

    const uint32_t total_height = hL + hH;
    assert(total_height > 1);

    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */
    int32_t s1n = bandL[0];
    int32_t d1n = bandH[0];
    int32_t s0n = s1n - ((d1n + 1) >> 1);

    uint32_t i = 0;
    if (total_height > 2) {
    	auto bL = bandL + strideL;
    	auto bH = bandH + strideH;
		for (uint32_t j = 0; i < (total_height - 3); i += 2, j++) {
			int32_t d1c = d1n;
			int32_t s0c = s0n;
			s1n = *bL;
			bL += strideL;
			d1n = *bH;
			bH += strideH;
			s0n = s1n - ((d1c + d1n + 2) >> 2);
			buf[i  ] = s0c;
			buf[i + 1] = d1c + ((s0c + s0n) >> 1);
		}
    }
    buf[i] = s0n;
    if (total_height & 1) {
        buf[total_height - 1] =
            bandL[((total_height - 1) / 2) * strideL] -
            ((d1n + 1) >> 1);
        buf[total_height - 2] = d1n + ((s0n + buf[total_height - 1]) >> 1);
    } else {
        buf[total_height - 1] = d1n + s0n;
    }
    for (i = 0; i < total_height; ++i) {
        *dest = buf[i];
        dest += strideDest;
    }
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
static void decompress_v_cas1_53(int32_t* buf,
                             int32_t *bandL,
							 const uint32_t hL,
							 const uint32_t strideL,
							 int32_t *bandH,
							 const uint32_t hH,
                             const uint32_t strideH,
							int32_t *dest,
							const uint32_t strideDest){

    const uint32_t total_height = hL + hH;
    assert(total_height > 2);

    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */
    int32_t s1 = bandH[strideH];
    int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
    buf[0] = bandH[0] + dc;
    auto s2_ptr = bandH + (strideH << 1);
    auto dn_ptr = bandL + strideL;
    uint32_t i, j;
    for (i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++) {
    	int32_t s2 = *s2_ptr;
    	s2_ptr += strideH;

    	int32_t dn = *dn_ptr - ((s1 + s2 + 2) >> 2);
    	dn_ptr += strideL;

        buf[i  ] = dc;
        buf[i + 1] = s1 + ((dn + dc) >> 1);
        dc = dn;
        s1 = s2;
    }
    buf[i] = dc;
    if (!(total_height & 1)) {
    	int32_t dn = bandL[((total_height>>1) - 1) * strideL] - ((s1 + 1) >> 1);
        buf[total_height - 2] = s1 + ((dn + dc) >> 1);
        buf[total_height - 1] = dn;
    } else {
        buf[total_height - 1] = s1 + dc;
    }
    for (i = 0; i < total_height; ++i) {
        *dest = buf[i];
        dest += strideDest;
    }
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decompress_h_53(const dwt_data<int32_t> *dwt,
                         int32_t *bandL,
						 int32_t *bandH,
						 int32_t *dest)
{
    const uint32_t total_width = dwt->sn + dwt->dn;
    if (dwt->cas == 0) { /* Left-most sample is on even coordinate */
        if (total_width > 1) {
            decompress_h_cas0_53(dwt->mem,bandL,dwt->sn, bandH, dwt->dn, dest);
        } else if (total_width == 1) {
        	//FIXME - validate this calculation
        	dest[0] = bandL[0];
        }
    } else { /* Left-most sample is on odd coordinate */
        if (total_width == 1) {
        	//FIXME - validate this calculation
        	dest[0] = bandH[0]/2;
        } else if (total_width == 2) {
            dwt->mem[1] = bandL[0] - ((bandH[0] + 1) >> 1);
            dest[0] = bandH[0] + dwt->mem[1];
            dest[1] = dwt->mem[1];
        } else if (total_width > 2) {
            decompress_h_cas1_53(dwt->mem, bandL, dwt->sn, bandH,dwt->dn, dest);
        }
    }
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decompress_v_53(const dwt_data<int32_t> *dwt,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest,
                         uint32_t nb_cols){
    const uint32_t sn = dwt->sn;
    const uint32_t len = sn + dwt->dn;
    if (dwt->cas == 0) {
        if (len == 1) {
            for (uint32_t c = 0; c < nb_cols; c++, bandL++,dest++)
                dest[0] = bandL[0];
            return;
        }
    	if (CPUArch::SSE2() || CPUArch::AVX2() ) {
#if (defined(__SSE2__) || defined(__AVX2__))
			if (len > 1 && nb_cols == PLL_COLS_53) {
				/* Same as below general case, except that thanks to SSE2/AVX2 */
				/* we can efficiently process 8/16 columns in parallel */
				decompress_v_cas0_mcols_SSE2_OR_AVX2_53(dwt->mem, bandL,sn, strideL, bandH, dwt->dn, strideH, dest, strideDest);
				return;
			}
#endif
    	}
        if (len > 1) {
            for (uint32_t c = 0; c < nb_cols; c++, bandL++, bandH++,dest++)
                decompress_v_cas0_53(dwt->mem, bandL,sn, strideL,bandH,dwt->dn, strideH, dest, strideDest);
            return;
        }
    } else {
        if (len == 1) {
            for (uint32_t c = 0; c < nb_cols; c++, bandL++,dest++)
                dest[0] = bandL[0] >> 1;
            return;
        }
        else if (len == 2) {
            auto out = dwt->mem;
            for (uint32_t c = 0; c < nb_cols; c++, bandL++,bandH++,dest++) {
                out[1] = bandL[0] - ((bandH[0] + 1) >> 1);
                dest[0] = bandH[0] + out[1];
                dest[1] = out[1];
            }
            return;
        }
        if (CPUArch::SSE2() || CPUArch::AVX2() ) {
#if (defined(__SSE2__) || defined(__AVX2__))
			if (nb_cols == PLL_COLS_53) {
				/* Same as below general case, except that thanks to SSE2/AVX2 */
				/* we can efficiently process 8/16 columns in parallel */
				decompress_v_cas1_mcols_SSE2_OR_AVX2_53(dwt->mem, bandL,sn, strideL,bandH,dwt->dn, strideH, dest, strideDest);
				return;
			}
#endif
        }
		for (uint32_t c = 0; c < nb_cols; c++, bandL++,bandH++,dest++)
			decompress_v_cas1_53(dwt->mem, bandL,sn,strideL,bandH, dwt->dn, strideH, dest, strideDest);
    }
}

static void decompress_h_strip_53(const dwt_data<int32_t> *horiz,
						 uint32_t hMin,
						 uint32_t hMax,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {
    for (uint32_t j = hMin; j < hMax; ++j){
        decompress_h_53(horiz, bandL, bandH, dest);
        bandL += strideL;
        bandH += strideH;
        dest += strideDest;
    }
}

static bool decompress_h_mt_53(uint32_t num_threads,
						size_t data_size,
						 dwt_data<int32_t> &horiz,
		 	 	 	 	 dwt_data<int32_t> &vert,
						 uint32_t rh,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {
    if (num_threads == 1 || rh <= 1) {
    	if (!horiz.mem){
    	    if (! horiz.alloc(data_size)) {
    	        GRK_ERROR("Out of memory");
    	        return false;
    	    }
    	    vert.mem = horiz.mem;
    	}
    	decompress_h_strip_53(&horiz,0,rh,bandL,strideL,bandH,strideH, dest, strideDest);
    } else {
        uint32_t num_jobs = (uint32_t)num_threads;
        if (rh < num_jobs)
            num_jobs = rh;
        uint32_t step_j = (rh / num_jobs);
		std::vector< std::future<int> > results;
		for(uint32_t j = 0; j < num_jobs; ++j) {
		   auto min_j = j * step_j;
           auto job = new decompress_job<int32_t, dwt_data<int32_t>>(horiz,
										bandL + min_j * strideL,
										strideL,
										bandH + min_j * strideH,
										strideH,
										nullptr,0,
										nullptr,0,
										dest + min_j * strideDest,
										strideDest,
										j * step_j,
										j < (num_jobs - 1U) ? (j + 1U) * step_j : rh);
            if (!job->data.alloc(data_size)) {
                GRK_ERROR("Out of memory");
                horiz.release();
                return false;
            }
			results.emplace_back(
				ThreadPool::get()->enqueue([job] {
					decompress_h_strip_53(&job->data,
							job->min_j,
							job->max_j,
							job->bandLL,
							job->strideLL,
							job->bandHL,
							job->strideHL,
							job->dest,
							job->strideDest);
				    job->data.release();
				    delete job;
					return 0;
				})
			);
		}
		for(auto &result: results)
			result.get();
    }
    return true;
}

static void decompress_v_strip_53(const dwt_data<int32_t> *vert,
						 uint32_t wMin,
						 uint32_t wMax,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {


    uint32_t j;
    for (j = wMin; j + PLL_COLS_53 <= wMax; j += PLL_COLS_53){
        decompress_v_53(vert, bandL, strideL, bandH, strideH,dest, strideDest, PLL_COLS_53);
		bandL += PLL_COLS_53;
		bandH += PLL_COLS_53;
		dest  += PLL_COLS_53;
    }
    if (j < wMax)
        decompress_v_53(vert, bandL, strideL, bandH, strideH, dest, strideDest, wMax - j);
}

static bool decompress_v_mt_53(uint32_t num_threads,
						size_t data_size,
						 dwt_data<int32_t> &horiz,
		 	 	 	 	 dwt_data<int32_t> &vert,
						 uint32_t rw,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {
    if (num_threads == 1 || rw <= 1) {
    	if (!horiz.mem){
    	    if (! horiz.alloc(data_size)) {
    	        GRK_ERROR("Out of memory");
    	        return false;
    	    }
    	    vert.mem = horiz.mem;
    	}
    	decompress_v_strip_53(&vert, 0, rw, bandL, strideL, bandH, strideH, dest, strideDest);
    } else {
        uint32_t num_jobs = (uint32_t)num_threads;
        if (rw < num_jobs)
            num_jobs = rw;
        uint32_t step_j = (rw / num_jobs);
		std::vector< std::future<int> > results;
        for (uint32_t j = 0; j < num_jobs; j++) {
			    auto min_j = j * step_j;
            auto job = new decompress_job<int32_t, dwt_data<int32_t>>(vert,
										bandL + min_j,
										strideL,
										nullptr,
										0,
										bandH + min_j,
										strideH,
										nullptr,
										0,
										dest + min_j,
										strideDest,
										j * step_j,
										j < (num_jobs - 1U) ? (j + 1U) * step_j : rw);
            if (!job->data.alloc(data_size)) {
                GRK_ERROR("Out of memory");
                vert.release();
                return false;
            }
			results.emplace_back(
				ThreadPool::get()->enqueue([job] {
					decompress_v_strip_53(&job->data,
							job->min_j,
							job->max_j,
							job->bandLL,
							job->strideLL,
							job->bandLH,
							job->strideLH,
							job->dest,
							job->strideDest);
					job->data.release();
					delete job;
				return 0;
				})
			);
        }
		for(auto &result: results)
			result.get();
    }
    return true;
}


/* <summary>                            */
/* Inverse wavelet transform in 2-D.    */
/* </summary>                           */
static bool decompress_tile_53( TileComponent* tilec, uint32_t numres){
    if (numres == 1U)
        return true;

    auto tr = tilec->resolutions;
    uint32_t rw = tr->width();
    uint32_t rh = tr->height();

    uint32_t num_threads = (uint32_t)ThreadPool::get()->num_threads();
    size_t data_size = max_resolution(tr, numres);
    /* overflow check */
    if (data_size > (SIZE_MAX / PLL_COLS_53 / sizeof(int32_t))) {
        GRK_ERROR("Overflow");
        return false;
    }
    /* We need PLL_COLS_53 times the height of the array, */
    /* since for the vertical pass */
    /* we process PLL_COLS_53 columns at a time */
    dwt_data<int32_t> horiz;
    dwt_data<int32_t> vert;
    data_size *= PLL_COLS_53 * sizeof(int32_t);
    bool rc = true;
    for (uint8_t res = 1; res < numres; ++res){
        horiz.sn = rw;
        vert.sn = rh;
        ++tr;
        rw = tr->width();
        rh = tr->height();
        if (rw == 0 || rh == 0)
        	continue;
        horiz.dn = rw - horiz.sn;
        horiz.cas = tr->x0 & 1;
    	if (!decompress_h_mt_53(num_threads,
    						data_size,
							horiz,
							vert,
							vert.sn,
							// LL
							tilec->getBuffer()->getWindow(res-1)->data,
							tilec->getBuffer()->getWindow(res-1)->stride,
							// HL
							tilec->getBuffer()->getWindow(res, BAND_ORIENT_HL)->data,
							tilec->getBuffer()->getWindow(res,BAND_ORIENT_HL)->stride,
							// lower split window
							tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->data,
							tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->stride))
    		return false;
    	if (!decompress_h_mt_53(num_threads,
    						data_size,
							horiz,
							vert,
							rh -  vert.sn,
							// LH
							tilec->getBuffer()->getWindow(res, BAND_ORIENT_LH)->data,
							tilec->getBuffer()->getWindow(res,BAND_ORIENT_LH)->stride,
							// HH
							tilec->getBuffer()->getWindow(res, BAND_ORIENT_HH)->data,
							tilec->getBuffer()->getWindow(res,BAND_ORIENT_HH)->stride,
							// higher split window
    						tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->data,
    						tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->stride ))
    		return false;
        vert.dn = rh - vert.sn;
        vert.cas = tr->y0 & 1;
    	if (!decompress_v_mt_53(num_threads,
    						data_size,
							horiz,
							vert,
							rw,
							// lower split window
							tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->data,
							tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->stride,
							// higher split window
							tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->data,
							tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->stride,
							// resolution buffer
							tilec->getBuffer()->getWindow(res)->data,
							tilec->getBuffer()->getWindow(res)->stride))
    		return false;
    }
    horiz.release();
    return rc;
}

//#undef __SSE__

#ifdef __SSE__
static void decompress_step1_sse_97(Params97 d,
                                    const __m128 c){
	// process 4 floats at a time
    __m128* GRK_RESTRICT mmData = (__m128*) d.data;
    uint32_t i;
    for (i = 0; i + 3 < d.len; i += 4, mmData += 8) {
    	mmData[0] = _mm_mul_ps(mmData[0], c);
    	mmData[2] = _mm_mul_ps(mmData[2], c);
    	mmData[4] = _mm_mul_ps(mmData[4], c);
    	mmData[6] = _mm_mul_ps(mmData[6], c);
    }
    for (; i < d.len; ++i, mmData += 2)
        mmData[0] = _mm_mul_ps(mmData[0], c);
}
#endif

static void decompress_step1_97(const Params97 &d,
                                const float c){

#ifdef __SSE__
	decompress_step1_sse_97(d, _mm_set1_ps(c));
#else
    float* GRK_RESTRICT fw = (float*) d.data;

    for (uint32_t i = 0; i < d.len; ++i, fw += 8) {
        fw[0] *= c;
        fw[1] *= c;
        fw[2] *= c;
        fw[3] *= c;;
    }
#endif
}


#ifdef __SSE__
static void decompress_step2_sse_97(const Params97 &d, __m128 c){
    __m128* GRK_RESTRICT vec_data = (__m128*) d.data;

    uint32_t imax = min<uint32_t>(d.len, d.lenMax);
    __m128 tmp1, tmp2, tmp3;
    if (d.absoluteStart == 0) {
        tmp1 = ((__m128*) d.dataPrev)[0];
    } else {
        tmp1 = vec_data[-3];
    }

    uint32_t i = 0;
    for (; i + 3 < imax; i += 4) {
        __m128 tmp4, tmp5, tmp6, tmp7, tmp8, tmp9;
        tmp2 = vec_data[-1];
        tmp3 = vec_data[ 0];
        tmp4 = vec_data[ 1];
        tmp5 = vec_data[ 2];
        tmp6 = vec_data[ 3];
        tmp7 = vec_data[ 4];
        tmp8 = vec_data[ 5];
        tmp9 = vec_data[ 6];
        vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
        vec_data[ 1] = _mm_add_ps(tmp4, _mm_mul_ps(_mm_add_ps(tmp3, tmp5), c));
        vec_data[ 3] = _mm_add_ps(tmp6, _mm_mul_ps(_mm_add_ps(tmp5, tmp7), c));
        vec_data[ 5] = _mm_add_ps(tmp8, _mm_mul_ps(_mm_add_ps(tmp7, tmp9), c));
        tmp1 = tmp9;
        vec_data += 8;
    }

    for (; i < imax; ++i) {
        tmp2 = vec_data[-1];
        tmp3 = vec_data[ 0];
        vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
        tmp1 = tmp3;
        vec_data += 2;
    }
    if (d.lenMax < d.len) {
        assert(d.lenMax + 1 == d.len);
        c = _mm_add_ps(c, c);
        c = _mm_mul_ps(c, vec_data[-2]);
        vec_data[-1] = _mm_add_ps(vec_data[-1], c);
    }
}
#endif


static void decompress_step2_97(const Params97 &d,float c){
#ifdef __SSE__
	decompress_step2_sse_97(d, _mm_set1_ps(c));
#else

    float* dataPrev = (float*) d.dataPrev;
    float* data = (float*) d.data;

    uint32_t imax = min<uint32_t>(d.len, d.lenMax);
    for (uint32_t i = 0; i < imax; ++i) {
        float tmp1_1 = dataPrev[0];
        float tmp1_2 = dataPrev[1];
        float tmp1_3 = dataPrev[2];
        float tmp1_4 = dataPrev[3];
        float tmp2_1 = data[-4];
        float tmp2_2 = data[-3];
        float tmp2_3 = data[-2];
        float tmp2_4 = data[-1];
        float tmp3_1 = data[0];
        float tmp3_2 = data[1];
        float tmp3_3 = data[2];
        float tmp3_4 = data[3];
        data[-4] = tmp2_1 + ((tmp1_1 + tmp3_1) * c);
        data[-3] = tmp2_2 + ((tmp1_2 + tmp3_2) * c);
        data[-2] = tmp2_3 + ((tmp1_3 + tmp3_3) * c);
        data[-1] = tmp2_4 + ((tmp1_4 + tmp3_4) * c);
        dataPrev = data;
        data += 8;
    }
    if (d.lenMax < d.len) {
        assert(d.lenMax + 1 == d.len);
        c += c;
        data[-4] = data[-4] + dataPrev[0] * c;
        data[-3] = data[-3] + dataPrev[1] * c;
        data[-2] = data[-2] + dataPrev[2] * c;
        data[-1] = data[-1] + dataPrev[3] * c;
    }
#endif
}

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 1-D. */
/* </summary>                            */
static void decompress_step_97(dwt_data<vec4f>* GRK_RESTRICT dwt)
{

    if ( (!dwt->cas && dwt->dn == 0 && dwt->sn <= 1) ||
    	 ( dwt->cas && dwt->sn == 0 && dwt->dn >= 1) )
            return;

    decompress_step1_97(makeParams97(dwt,true,true),  K);
    decompress_step1_97(makeParams97(dwt,false,true), c13318);
    decompress_step2_97(makeParams97(dwt,true,false), dwt_delta);
    decompress_step2_97(makeParams97(dwt,false,false),dwt_gamma);
    decompress_step2_97(makeParams97(dwt,true,false), dwt_beta);
    decompress_step2_97(makeParams97(dwt,false,false),dwt_alpha);
}

static void interleave_h_97(dwt_data<vec4f>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
                                   uint32_t remaining_height){
    float* GRK_RESTRICT bi = (float*)(dwt->mem + dwt->cas);
    uint32_t x0 = dwt->win_l_0;
    uint32_t x1 = dwt->win_l_1;

    for (uint32_t k = 0; k < 2; ++k) {
    	auto band = (k == 0) ? bandL : bandH;
    	uint32_t stride = (k == 0) ? strideL : strideH;
        if (remaining_height >= 4 && ((size_t) band & 0x0f) == 0 &&
                ((size_t) bi & 0x0f) == 0 && (stride & 0x0f) == 0) {
            /* Fast code path */
            for (uint32_t i = x0; i < x1; ++i, bi+=8) {
                uint32_t j = i;
                bi[0] = band[j];
                j += stride;
                bi[1] = band[j];
                j += stride;
                bi[2] = band[j];
                j += stride;
                bi[3] = band[j];
             }
        } else {
            /* Slow code path */
            for (uint32_t i = x0; i < x1; ++i, bi+=8) {
                uint32_t j = i;
                bi[0] = band[j];
                j += stride;
                if (remaining_height == 1)
                    continue;
                bi[1] = band[j];
                j += stride;
                if (remaining_height == 2)
                    continue;
                 bi[2] = band[j];
                j += stride;
                if (remaining_height == 3)
                    continue;
                bi[3] = band[j];
            }
        }

        bi = (float*)(dwt->mem + 1 - dwt->cas);
        x0 = dwt->win_h_0;
        x1 = dwt->win_h_1;
    }
}

static void decompress_h_strip_97(dwt_data<vec4f>* GRK_RESTRICT horiz,
								   const uint32_t rh,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
								   float* dest,
								   const size_t strideDest){
	uint32_t j;
	for (j = 0; j< (rh & (uint32_t)(~3)); j += 4) {
		interleave_h_97(horiz, bandL,strideL, bandH, strideH, rh - j);
		decompress_step_97(horiz);
		for (uint32_t k = 0; k <  horiz->sn + horiz->dn; k++) {
			dest[k      ] 					= horiz->mem[k].f[0];
			dest[k + (size_t)strideDest  ] 	= horiz->mem[k].f[1];
			dest[k + (size_t)strideDest * 2] 	= horiz->mem[k].f[2];
			dest[k + (size_t)strideDest * 3] 	= horiz->mem[k].f[3];
		}
		bandL += strideL << 2;
		bandH += strideH << 2;
		dest  += strideDest << 2;
	}
	if (j < rh) {
		interleave_h_97(horiz, bandL,strideL,bandH, strideH, rh - j);
		decompress_step_97(horiz);
		for (uint32_t k = 0; k < horiz->sn + horiz->dn; k++) {
			switch (rh - j) {
			case 3:
				dest[k + strideDest * 2] = horiz->mem[k].f[2];
			/* FALLTHRU */
			case 2:
				dest[k + strideDest  ] = horiz->mem[k].f[1];
			/* FALLTHRU */
			case 1:
				dest[k] = horiz->mem[k].f[0];
			}
		}
	}
}
static bool decompress_h_mt_97(uint32_t num_threads,
							size_t data_size,
							dwt_data<vec4f> &GRK_RESTRICT horiz,
						   const uint32_t rh,
						   float* GRK_RESTRICT bandL,
						   const uint32_t strideL,
						   float* GRK_RESTRICT bandH,
						   const uint32_t strideH,
						   float* GRK_RESTRICT dest,
						   const uint32_t strideDest){
	uint32_t num_jobs = num_threads;
    if (rh < num_jobs)
        num_jobs = rh;
    uint32_t step_j = num_jobs ? (rh / num_jobs) : 0;
    if (num_threads == 1 || step_j < 4) {
    	decompress_h_strip_97(&horiz, rh, bandL,strideL, bandH, strideH, dest, strideDest);
    } else {
		std::vector< std::future<int> > results;
		for(uint32_t j = 0; j < num_jobs; ++j) {
		   auto min_j = j * step_j;
		   auto job = new decompress_job<float, dwt_data<vec4f>>(horiz,
										bandL + min_j * strideL,
										strideL,
										bandH + min_j * strideH,
										strideH,
										nullptr,
										0,
										nullptr,
										0,
										dest + min_j * strideDest,
										strideDest,
										0,
										(j < (num_jobs - 1U) ? (j + 1U) * step_j : rh) - min_j);
			if (!job->data.alloc(data_size)) {
				GRK_ERROR("Out of memory");
				horiz.release();
				return false;
			}
			results.emplace_back(
				ThreadPool::get()->enqueue([job] {
	        		decompress_h_strip_97(&job->data,
	        				job->max_j,
							job->bandLL,
							job->strideLL,
							job->bandHL,
							job->strideHL,
							job->dest,
							job->strideDest);
					job->data.release();
					delete job;
					return 0;
				})
			);
		}
		for(auto &result: results)
			result.get();
    }
    return true;
}

static void interleave_v_97(dwt_data<vec4f>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
                                   uint32_t nb_elts_read){
    vec4f* GRK_RESTRICT bi = dwt->mem + dwt->cas;
    auto band = bandL + dwt->win_l_0 * strideL;
    for (uint32_t i = dwt->win_l_0; i < dwt->win_l_1; ++i, bi+=2) {
        memcpy((float*)bi, band, nb_elts_read * sizeof(float));
        band +=strideL;
    }

    bi = dwt->mem + 1 - dwt->cas;
    band = bandH + dwt->win_h_0 * strideH;
    for (uint32_t i = dwt->win_h_0; i < dwt->win_h_1; ++i, bi+=2) {
        memcpy((float*)bi, band, nb_elts_read * sizeof(float));
        band += strideH;
    }
}
static void decompress_v_strip_97(dwt_data<vec4f>* GRK_RESTRICT vert,
								   const uint32_t rw,
								   const uint32_t rh,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
								   float* GRK_RESTRICT dest,
								   const uint32_t strideDest){
    uint32_t j;
	for (j = 0; j < (rw & (uint32_t)~3); j += 4) {
		interleave_v_97(vert, bandL,strideL, bandH,strideH, 4);
		decompress_step_97(vert);
		auto destPtr = dest;
		for (uint32_t k = 0; k < rh; ++k){
			memcpy(destPtr, vert->mem+k, 4 * sizeof(float));
			destPtr += strideDest;
		}
		bandL += 4;
		bandH += 4;
		dest  += 4;
	}
	if (j < rw) {
		j = rw & 3;
		interleave_v_97(vert, bandL, strideL,bandH, strideH, j);
		decompress_step_97(vert);
		auto destPtr = dest;
		for (uint32_t k = 0; k < rh; ++k) {
			memcpy(destPtr, vert->mem+k,j * sizeof(float));
			destPtr += strideDest;
		}
	}
}

static bool decompress_v_mt_97(uint32_t num_threads,
							size_t data_size,
							dwt_data<vec4f> &GRK_RESTRICT vert,
							const uint32_t rw,
						   const uint32_t rh,
						   float* GRK_RESTRICT bandL,
						   const uint32_t strideL,
						   float* GRK_RESTRICT bandH,
						   const uint32_t strideH,
						   float* GRK_RESTRICT dest,
						   const uint32_t strideDest){
	auto num_jobs = (uint32_t)num_threads;
	if (rw < num_jobs)
		num_jobs = rw;
	auto step_j = num_jobs ? (rw / num_jobs) : 0;
	if (num_threads == 1 || step_j < 4) {
		decompress_v_strip_97(&vert,
							rw,
							rh,
							bandL,
							strideL,
							bandH,
							strideH,
							dest,
							strideDest);
	} else {
		std::vector< std::future<int> > results;
		for (uint32_t j = 0; j < num_jobs; j++) {
			auto min_j = j * step_j;
			auto job = new decompress_job<float, dwt_data<vec4f>>(vert,
														bandL + min_j,
														strideL,
														nullptr,
														0,
														bandH + min_j,
														strideH,
														nullptr,
														0,
														dest + min_j,
														strideDest,
														0,
														(j < (num_jobs - 1U) ? (j + 1U) * step_j : rw) - min_j);
			if (!job->data.alloc(data_size)) {
				GRK_ERROR("Out of memory");
				vert.release();
				return false;
			}
			results.emplace_back(
				ThreadPool::get()->enqueue([job,rh] {
					decompress_v_strip_97(&job->data,
									job->max_j,
									rh,
									job->bandLL,
									job->strideLL,
									job->bandLH,
									job->strideLH,
									job->dest,
									job->strideDest);
					job->data.release();
					delete job;
					return 0;
				})
			);
		}
		for(auto &result: results)
			result.get();
	}

	return true;
}

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 2-D. */
/* </summary>                            */
static
bool decompress_tile_97(TileComponent* GRK_RESTRICT tilec,uint32_t numres){
    if (numres == 1U)
        return true;

    auto tr = tilec->resolutions;
    uint32_t rw = tr->width();
    uint32_t rh = tr->height();

    size_t data_size = max_resolution(tr, numres);
    dwt_data<vec4f> horiz;
    dwt_data<vec4f> vert;
    if (!horiz.alloc(data_size)) {
        GRK_ERROR("Out of memory");
        return false;
    }
    vert.mem = horiz.mem;
    uint32_t num_threads = (uint32_t)ThreadPool::get()->num_threads();
    for (uint8_t res = 1; res < numres; ++res) {
        horiz.sn = rw;
        vert.sn = rh;
        ++tr;
        rw = tr->width();
        rh = tr->height();
        if (rw == 0 || rh == 0)
        	continue;
        horiz.dn = rw - horiz.sn;
        horiz.cas = tr->x0 & 1;
        horiz.win_l_0 = 0;
        horiz.win_l_1 = horiz.sn;
        horiz.win_h_0 = 0;
        horiz.win_h_1 = horiz.dn;
        if (!decompress_h_mt_97(num_threads,
        					data_size,
							horiz,
							vert.sn,
							// LL
							(float*) tilec->getBuffer()->getWindow(res-1)->data,
							tilec->getBuffer()->getWindow(res-1)->stride,
							// HL
							(float*) tilec->getBuffer()->getWindow(res, BAND_ORIENT_HL)->data,
							tilec->getBuffer()->getWindow(res,BAND_ORIENT_HL)->stride,
							// lower split window
							(float*) tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->data,
							tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->stride))
        	return false;
        if (!decompress_h_mt_97(num_threads,
        					data_size,
							horiz,
							rh-vert.sn,
							// LH
							(float*) tilec->getBuffer()->getWindow(res, BAND_ORIENT_LH)->data,
							tilec->getBuffer()->getWindow(res,BAND_ORIENT_LH)->stride,
							// HH
							(float*) tilec->getBuffer()->getWindow(res, BAND_ORIENT_HH)->data,
							tilec->getBuffer()->getWindow(res,BAND_ORIENT_HH)->stride,
							// higher split window
							(float*) tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->data,
							tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->stride ))
        	return false;
        vert.dn = rh - vert.sn;
        vert.cas = tr->y0 & 1;
        vert.win_l_0 = 0;
        vert.win_l_1 = vert.sn;
        vert.win_h_0 = 0;
        vert.win_h_1 = vert.dn;
        if (!decompress_v_mt_97(num_threads,
        					data_size,
							vert,
							rw,
							rh,
							// lower split window
							(float*) tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->data,
							tilec->getBuffer()->getSplitWindow(res,SPLIT_L)->stride,
							// higher split window
							(float*) tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->data,
							tilec->getBuffer()->getSplitWindow(res,SPLIT_H)->stride,
							// resolution window
							(float*) tilec->getBuffer()->getWindow(res)->data,
							tilec->getBuffer()->getWindow(res)->stride))
        	return false;
    }
    horiz.release();
    return true;
}

/**
 * ************************************************************************************
 *
 * 5/3 operates on elements of type int32_t while 9/7 operates on elements of type vec4f
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, with height
 *   5/3
 *   Height : sizeof(T)/sizeof(int32_t)
 *
 *   9/7
 *   Height : sizeof(T)/sizeof(int32_t)
 *
 * Vertical pass
 *
 * Each thread processes a strip running the height of the window, with width
 *
 *  5/3
 *  Width :  4
 *
 *  9/7
 *  Width :  4
 *
 ****************************************************************************/
template<typename T> class PartialInterleaver {
public:
	/**
	 * interleaved data is laid out in the dwt->mem buffer in increments of
	 * type T
	 */
	void interleave_h(dwt_data<T>* dwt,
								ISparseBuffer* sa,
								uint32_t y_offset,
								uint32_t y_num_rows){
		const uint32_t typeSize = (uint32_t)(sizeof(T)/sizeof(int32_t));
	    for (uint32_t i = 0; i < y_num_rows; i++) {
	    	// read one row of L band and write interleaved
	        bool ret = sa->read(dwt->win_l_0,
							  y_offset + i,
							  dwt->win_l_1,
							  y_offset + i + 1,
							  (int32_t*)dwt->memLow + i,
							  2 * typeSize,
							  0,
							  true);
	        assert(ret);
	        // read one row of H band and write interleaved
	        ret = sa->read(dwt->sn + dwt->win_h_0,
							  y_offset + i,
							  dwt->sn + dwt->win_h_1,
							  y_offset + i + 1,
							  (int32_t*)dwt->memHigh + i,
							  2 * typeSize,
							  0,
							  true);
	        assert(ret);
	        GRK_UNUSED(ret);
	    }
	}
	/*
	 * interleaved data is laid out in the dwt->mem buffer in
	 * increments of VERT_PASS_WIDTH elements of type int32_t/float
	 *
	 */
	void interleave_v(dwt_data<T>* GRK_RESTRICT dwt,
								ISparseBuffer* sa,
								uint32_t x_offset,
								uint32_t x_num_elements){
	    const uint32_t VERT_PASS_WIDTH = 4;
		assert(x_num_elements <= VERT_PASS_WIDTH);
    	// read one vertical strip (of width x_num_elements <= VERT_PASS_WIDTH) of L band and write interleaved
	    bool ret = sa->read(x_offset,
	    					dwt->win_l_0,
							x_offset + x_num_elements,
							dwt->win_l_1,
							(int32_t*)dwt->memLow,
							1,
							2 * VERT_PASS_WIDTH,
							true);
	    assert(ret);
    	// read one vertical strip (of width x_num_elements) of H band and write interleaved
	    ret = sa->read(x_offset,
	    				dwt->sn + dwt->win_h_0,
						x_offset + x_num_elements,
						dwt->sn + dwt->win_h_1,
						(int32_t*)dwt->memHigh,
						1,
						2 * VERT_PASS_WIDTH,
						true);
	    assert(ret);
	    GRK_UNUSED(ret);
	}
};


template<typename T> class Partial53 : public PartialInterleaver<T> {
public:
	void decompress_h(dwt_data<T>* horiz){

		#define S(i) 	buf[(i)<<1]
		#define D(i) 	buf[(1+((i)<<1))]

		#define S_(i) 	((i)<0 ? S(0) :	((i)>=sn ? S(sn-1) : S(i)))
		#define D_(i) 	((i)<0 ? D(0) :	((i)>=dn ? D(dn-1) : D(i)))

		#define SS_(i)	((i)<0 ? S(0) :	((i)>=dn ? S(dn-1) : S(i)))
		#define DD_(i) 	((i)<0 ? D(0) :	((i)>=sn ? D(sn-1) : D(i)))

		int32_t i;
		int32_t dn 		 = (int32_t)horiz->dn;
		int32_t sn 		 = (int32_t)horiz->sn;
		int32_t cas 	 = (int32_t)horiz->cas;
		int32_t win_l_x0 = (int32_t)horiz->win_l_0;
		int32_t win_l_x1 = (int32_t)horiz->win_l_1;
		int32_t win_h_x0 = (int32_t)horiz->win_h_0;
		int32_t win_h_x1 = (int32_t)horiz->win_h_1;

		if (!cas) {
			if ((dn > 0) || (sn > 1)) {
				/* Naive version is :
				for (i = win_l_x0; i < i_max; i++) {
					S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
				}
				for (i = win_h_x0; i < win_h_x1; i++) {
					D(i) += (S_(i) + S_(i + 1)) >> 1;
				}
				but the compiler doesn't manage to unroll it to avoid bound
				checking in S_ and D_ macros
				*/

				auto buf	 = horiz->memLow;
				i = 0;
				if (i < win_l_x1 - win_l_x0) {
					int32_t i_max;

					/* Left-most case */
					S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
					i ++;

					i_max = win_l_x1 - win_l_x0;
					if (i_max > dn - win_l_x0)
						i_max = dn - win_l_x0;
					for (; i < i_max; i++) {
						/* No bound checking */
						S(i) -= (D(i - 1) + D(i) + 2) >> 2;
					}
					for (; i < win_l_x1 - win_l_x0; i++) {
						/* Right-most case */
						S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
					}
				}

				buf	 = horiz->memHigh;
				i = 0;
				if (i < win_h_x1 - win_h_x0) {
					int32_t i_max = win_h_x1 - win_h_x0;
					if (i_max >= sn - win_h_x0)
						i_max = sn - 1 - win_h_x0;
					for (; i < i_max; i++) {
						/* No bound checking */
						D(i) += (S(i) + S(i + 1)) >> 1;
					}
					for (; i < win_h_x1 - win_h_x0; i++) {
						/* Right-most case */
						D(i) += (S_(i) + S_(i + 1)) >> 1;
					}
				}
			}
		} else {
			if (!sn  && dn == 1) {
				auto buf = horiz->memLow;
				S(0) /= 2;
			} else {
				auto buf = horiz->memLow;
				for (i = 0; i < win_l_x1 - win_l_x0; i++)
					D(i) -= (SS_(i) + SS_(i + 1) + 2) >> 2;
				buf	 = horiz->memHigh;
				for (i = win_h_x0; i < win_h_x1 - win_h_x0; i++)
					S(i) += (DD_(i) + DD_(i - 1)) >> 1;
			}
		}
	}
	void decompress_v(dwt_data<T>* vert){
	    const uint32_t VERT_PASS_WIDTH = 4;

		#define S_off(i,off) 		buf[(i)*2 * VERT_PASS_WIDTH + off]
		#define D_off(i,off) 		buf[(1+(i)*2)*VERT_PASS_WIDTH + off]

		#define S_off_(i,off) 		(((i)>=sn ? S_off(sn-1,off) : S_off(i,off)))
		#define D_off_(i,off) 		(((i)>=dn ? D_off(dn-1,off) : D_off(i,off)))

		#define S_sgnd_off_(i,off) 	(((i)<0   ? S_off(0,off)    : S_off_(i,off)))
		#define D_sgnd_off_(i,off) 	(((i)<0	  ? D_off(0,off)    : D_off_(i,off)))

		#define SS_sgnd_off_(i,off)  ((i)<0   ? S_off(0,off)    : ((i)>=dn ? S_off(dn-1,off) : S_off(i,off)))
		#define DD_sgnd_off_(i,off)  ((i)<0   ? D_off(0,off)    : ((i)>=sn ? D_off(sn-1,off) : D_off(i,off)))

		#define SS_off_(i,off) 		(((i)>=dn ? S_off(dn-1,off) : S_off(i,off)))
		#define DD_off_(i,off) 		(((i)>=sn ? D_off(sn-1,off) : D_off(i,off)))

		uint32_t i;
		uint32_t dn 	  = vert->dn;
		uint32_t sn 	  = vert->sn;
		uint32_t cas 	  = vert->cas;
		uint32_t win_l_x0 = vert->win_l_0;
		uint32_t win_l_x1 = vert->win_l_1;
		uint32_t win_h_x0 = vert->win_h_0;
		uint32_t win_h_x1 = vert->win_h_1;

		if (!cas) {
			if ((dn > 0) || (sn > 1)) {
				/* Naive version is :
				for (i = win_l_x0; i < i_max; i++) {
					S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
				}
				for (i = win_h_x0; i < win_h_x1; i++) {
					D(i) += (S_(i) + S_(i + 1)) >> 1;
				}
				but the compiler doesn't manage to unroll it to avoid bound
				checking in S_ and D_ macros
				*/

				// 1. low pass
				auto buf   = vert->memLow;
				i = 0;
				if (i < win_l_x1 - win_l_x0) {
					uint32_t i_max;

					/* Left-most case */
					for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
						S_off(i, off) -= (D_sgnd_off_((int64_t)i - 1, off) + D_off_(i, off) + 2) >> 2;
					i ++;

					i_max = win_l_x1 - win_l_x0;
					if (i_max > dn - win_l_x0)
						i_max = dn - win_l_x0;
		#ifdef __SSE2__
					if (i + 1 < i_max) {
						const __m128i two = _mm_set1_epi32(2);
						auto Dm1 = _mm_load_si128((__m128i *)(buf + VERT_PASS_WIDTH + ((int64_t)i - 1) * 2 * VERT_PASS_WIDTH));
						for (; i + 1 < i_max; i += 2) {
							/* No bound checking */
							auto S = _mm_load_si128((__m128i *)(buf +  (i * 2) * VERT_PASS_WIDTH));
							auto D = _mm_load_si128((__m128i *)(buf +  (i * 2 + 1) * VERT_PASS_WIDTH));
							auto S1 = _mm_load_si128((__m128i *)(buf + (i * 2 + 2) * VERT_PASS_WIDTH));
							auto D1 = _mm_load_si128((__m128i *)(buf + (i * 2 + 3) * VERT_PASS_WIDTH));
							S  = _mm_sub_epi32(S,  _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(Dm1, D), two), 2));
							S1 = _mm_sub_epi32(S1, _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(D, D1), two), 2));
							_mm_store_si128((__m128i*)(buf + i * 2 * VERT_PASS_WIDTH), S);
							_mm_store_si128((__m128i*)(buf + (i + 1) * 2 * VERT_PASS_WIDTH), S1);
							Dm1 = D1;
						}
					}
		#endif
					for (; i < i_max; i++) {
						/* No bound checking */
						for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							S_off(i, off) -= (D_sgnd_off_((int64_t)i - 1, off) + D_off(i, off) + 2) >> 2;
					}
					for (; i < win_l_x1 - win_l_x0; i++) {
						/* Right-most case */
						for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							S_off(i, off) -= (D_sgnd_off_((int64_t)i - 1, off) + D_off_(i, off) + 2) >> 2;
					}
				}

				// 2. high pass
				buf = vert->memHigh;
				i = 0;
				if (i < win_h_x1 - win_h_x0) {
					uint32_t i_max = win_h_x1 - win_h_x0;
					if (i_max >= sn - win_h_x0)
						i_max = sn - 1 - win_h_x0;
		#ifdef __SSE2__
					if (i + 1 < i_max) {
						auto S =  _mm_load_si128((__m128i *)(buf + i * 2 * VERT_PASS_WIDTH));
						for (; i + 1 < i_max; i += 2) {
							/* No bound checking */
							auto D =  _mm_load_si128((__m128i *)(buf + (1 + i * 2) * VERT_PASS_WIDTH ));
							auto S1 = _mm_load_si128((__m128i *)(buf + ((i + 1) * 2) * VERT_PASS_WIDTH ));
							auto D1 = _mm_load_si128((__m128i *)(buf + (1 + (i + 1) * 2) * VERT_PASS_WIDTH ));
							auto S2 = _mm_load_si128((__m128i *)(buf + ((i + 2) * 2) * VERT_PASS_WIDTH) );
							D  = _mm_add_epi32(D,  _mm_srai_epi32(_mm_add_epi32(S, S1),  1));
							D1 = _mm_add_epi32(D1, _mm_srai_epi32(_mm_add_epi32(S1, S2), 1));
							_mm_store_si128((__m128i*)(buf + (1 + i * 2) * VERT_PASS_WIDTH), D);
							_mm_store_si128((__m128i*)(buf + (1 + (i + 1) * 2) * VERT_PASS_WIDTH ), D1);
							S = S2;
						}
					}
		#endif
					for (; i < i_max; i++) {
						/* No bound checking */
						for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							D_off(i, off) += (S_off(i, off) + S_off(i + 1, off)) >> 1;
					}
					for (; i < win_h_x1  - win_h_x0; i++) {
						/* Right-most case */
						for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							D_off(i, off) += (S_off_(i, off) + S_off_(i + 1, off)) >> 1;
					}
				}
			}
		} else {
			if (!sn  && dn == 1) {
				// edge case at origin
				auto buf   = vert->memLow;
				for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
					S_off(0, off) /= 2;
			} else {
				// 1. low pass
				auto buf   = vert->memLow;
				for (i = 0; i < win_l_x1 - win_l_x0; i++) {
					for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
						D_off(i, off) -= (SS_off_(i, off) + SS_off_(i + 1, off) + 2) >> 2;
				}
				// 2. high pass
				buf   = vert->memHigh;
				for (i = 0; i < win_h_x1 - win_h_x0; i++) {
					for (uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
						S_off(i, off) += (DD_off_(i, off) + DD_sgnd_off_((int64_t)i - 1, off)) >> 1;
				}
			}
		}
	}
};


template<typename T> class Partial97 : public PartialInterleaver<T> {
public:
	void decompress_h(dwt_data<T>* dwt){
		decompress_step_97(dwt);
	}
	void decompress_v(dwt_data<T>* dwt){
		decompress_step_97(dwt);
	}
};

// note: dwt->memLow and dwt->memHigh are only set for partial decode
static Params97 makeParams97(dwt_data<vec4f>* dwt,
							bool low,
							bool step1){
	Params97 rc;
	if (low){
		rc.data = dwt->memLow ? dwt->memLow : dwt->mem;
		if (step1) {
			rc.data += dwt->memLow ?   (dwt->cas + dwt->win_l_0) :  (dwt->cas + 2 * dwt->win_l_0);
			rc.len  = dwt->win_l_1 - dwt->win_l_0;
		} else {
			rc.data += dwt->cas + 1;
			// handle reflection at boundary
			rc.dataPrev = rc.data -(dwt->cas + 1) + (!dwt->cas);
			rc.len = dwt->win_l_1 - dwt->win_l_0;
			rc.lenMax = (uint32_t)(min<int32_t>((int32_t)dwt->sn, (int32_t)dwt->dn - (int32_t)dwt->cas) - (int32_t)dwt->win_l_0);

		    if (dwt->win_l_0 > 0) {
		        rc.data += dwt->win_l_0;
		        rc.dataPrev = rc.data - 2;
		    }
		    rc.absoluteStart = dwt->win_l_0;
		}
	} else {
		rc.data = dwt->memHigh ? dwt->memHigh : dwt->mem;
		if (step1) {
			rc.data += dwt->memHigh ? ((!dwt->cas) + dwt->win_h_0) :  ((!dwt->cas) + 2 * dwt->win_h_0);
			rc.len = dwt->win_h_1 - dwt->win_h_0;
		} else {
			rc.data += (!dwt->cas) + 1;
			// handle reflection at boundary
			rc.dataPrev = rc.data - ((!dwt->cas) + 1) + dwt->cas;
			rc.len = dwt->win_h_1 - dwt->win_h_0;
			rc.lenMax = (uint32_t)(min<int32_t>((int32_t)dwt->dn, (int32_t)dwt->sn - (int32_t)(!dwt->cas)) - (int32_t)dwt->win_h_0);

		    if (dwt->win_h_0 > 0) {
		        rc.data += dwt->win_h_0;
		        rc.dataPrev = rc.data - 2;
		    }
			rc.absoluteStart = dwt->win_h_0;
		}
	}

	return rc;
};

/**
 * ************************************************************************************
 *
 * 5/3 operates on elements of type int32_t while 9/7 operates on elements of type vec4f
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, of the following dimensions:
 *
 *   5/3
 *   Height : 1
 *
 *   9/7
 *   Height : 4
 *
 * Vertical pass
 *
 *  5/3
 *  Width :  4
 *
 *  9/7
 *  Height : 1
 *
 ****************************************************************************
 *
 * FILTER_WIDTH value matches the maximum left/right extension given in tables
 * F.2 and F.3 of the standard
 */
template <typename T,
			uint32_t FILTER_WIDTH,
			typename D>

   bool decompress_partial_tile(TileComponent* GRK_RESTRICT tilec,
		   	   	   	   	   grk_rect_u32 bounds,
		   	   	   	   	   uint32_t numres,
						   ISparseBuffer *sa) {

	bool rc = false;
	uint8_t numresolutions = tilec->numresolutions;
    auto fullRes 	= tilec->resolutions;
    auto fullResTopLevel = tilec->resolutions + numres - 1;
    if (!fullResTopLevel->width() || !fullResTopLevel->height()){
        return true;
    }

    const uint32_t HORIZ_PASS_HEIGHT = sizeof(T)/sizeof(int32_t);
    const uint32_t VERT_PASS_WIDTH = 4;
    const uint32_t pad = FILTER_WIDTH * VERT_PASS_WIDTH;

	auto synthesisWindow = bounds;
	synthesisWindow = synthesisWindow.rectceildivpow2(numresolutions - 1 - (numres-1));

	assert(fullResTopLevel->intersection(synthesisWindow) == synthesisWindow);
    synthesisWindow = synthesisWindow.pan(-(int64_t)fullResTopLevel->x0,-(int64_t)fullResTopLevel->y0);

	try {
    if (numres == 1U) {
        // simply copy into tile component buffer
    	bool ret = sa->read(synthesisWindow,
					   tilec->getBuffer()->getWindow()->data,
                       1,
					   tilec->getBuffer()->getWindow()->stride,
                       true);
        assert(ret);
        GRK_UNUSED(ret);
        return true;
    }
	} catch (MissingSparseBlockException &ex){
		return false;
	}

    D decompressor;
    size_t num_threads = ThreadPool::get()->num_threads();

    try {

    for (uint8_t resno = 1; resno < numres; resno ++) {
    	auto fullResLower = fullRes;
    	dwt_data<T> horiz;
    	dwt_data<T> vert;

        horiz.sn 	= fullResLower->width();
        vert.sn 	= fullResLower->height();
        ++fullRes;
        horiz.dn 	= fullRes->width() - horiz.sn;
        horiz.cas 	= fullRes->x0 & 1;
        vert.dn 	= fullRes->height() - vert.sn;
        vert.cas 	= fullRes->y0 & 1;

        // 1. set up windows for horizontal and vertical passes
        grk_rect_u32 bandWindowRect[BAND_NUM_ORIENTATIONS];
        bandWindowRect[BAND_ORIENT_LL] = *((grk_rect_u32*)tilec->getBuffer()->getWindow(resno,BAND_ORIENT_LL ));
        bandWindowRect[BAND_ORIENT_HL] = *((grk_rect_u32*)tilec->getBuffer()->getWindow(resno,BAND_ORIENT_HL ));
        bandWindowRect[BAND_ORIENT_LH] = *((grk_rect_u32*)tilec->getBuffer()->getWindow(resno,BAND_ORIENT_LH ));
        bandWindowRect[BAND_ORIENT_HH] = *((grk_rect_u32*)tilec->getBuffer()->getWindow(resno,BAND_ORIENT_HH ));

        // band windows in tile coordinates - needed to pre-allocate sparse blocks
        grk_rect_u32 tileBandWindowRect[BAND_NUM_ORIENTATIONS];
        tileBandWindowRect[BAND_ORIENT_LL]  =  bandWindowRect[BAND_ORIENT_LL];
        tileBandWindowRect[BAND_ORIENT_HL]  =  bandWindowRect[BAND_ORIENT_HL].pan(fullRes->band[BAND_INDEX_LH].width(),0);
        tileBandWindowRect[BAND_ORIENT_LH]  =  bandWindowRect[BAND_ORIENT_LH].pan(0,fullRes->band[BAND_INDEX_HL].height());
        tileBandWindowRect[BAND_ORIENT_HH]  =  bandWindowRect[BAND_ORIENT_HH].pan(fullRes->band[BAND_INDEX_LH].width(),fullRes->band[BAND_INDEX_HL].height());

        grk_rect_u32 resWindowRect = *((grk_rect_u32*)tilec->getBuffer()->getWindow(resno));

        // two windows formed by horizontal pass and used as input for vertical pass
        grk_rect_u32 splitWindowRect[SPLIT_NUM_ORIENTATIONS];
        splitWindowRect[SPLIT_L] = *((grk_rect_u32*)tilec->getBuffer()->getSplitWindow(resno,SPLIT_L ));
        splitWindowRect[SPLIT_H] = *((grk_rect_u32*)tilec->getBuffer()->getSplitWindow(resno,SPLIT_H ));

        // 2. pre-allocate sparse blocks
        for (uint32_t i = 0; i < BAND_NUM_ORIENTATIONS; ++i){
        	auto temp = tileBandWindowRect[i];
            if (!sa->alloc(temp.grow(FILTER_WIDTH, fullRes->width(),  fullRes->height())))
    			 goto cleanup;
        }
        if (!sa->alloc(resWindowRect))
			goto cleanup;
		for (uint32_t k = 0; k < SPLIT_NUM_ORIENTATIONS; ++k) {
			 auto temp = splitWindowRect[k];
			 if (!sa->alloc(temp.grow(FILTER_WIDTH, fullRes->width(),  fullRes->height())))
					goto cleanup;
		}

		auto executor_h = [sa, resWindowRect, &decompressor](decompress_job<float, dwt_data<T>> *job){
			 try {
				 for (uint32_t j = job->min_j; j < job->max_j; j += HORIZ_PASS_HEIGHT) {
					 auto height = std::min<uint32_t>((uint32_t)HORIZ_PASS_HEIGHT,job->max_j - j );
					 job->data.memLow 	=  job->data.mem +   job->data.cas;
					 job->data.memHigh  =  job->data.mem + (!job->data.cas) + 2 * job->data.win_h_0 - 2 * job->data.win_l_0;
					 decompressor.interleave_h(&job->data, sa, j,height);
					 job->data.memLow 	=  job->data.mem - job->data.win_l_0;
					 job->data.memHigh  =  job->data.mem + job->data.win_h_0 - 2 * job->data.win_l_0;
					 decompressor.decompress_h(&job->data);
					 if (!sa->write( resWindowRect.x0,
									  j,
									  resWindowRect.x1,
									  j + height,
									  (int32_t*)(job->data.mem + resWindowRect.x0 - 2 * job->data.win_l_0),
									  HORIZ_PASS_HEIGHT,
									  1,
									  true)) {
						 GRK_ERROR("sparse array write failure");
						 job->data.release();
						 delete job;
						 return 1;
					 }
				 }
				  job->data.release();
				  delete job;
				  return 0;
			 } catch (MissingSparseBlockException &msbe){
				  job->data.release();
				  delete job;
				  return 1;
			 }
		};

		auto executor_v = [sa, resWindowRect, &decompressor](decompress_job<float, dwt_data<T>> *job){
			 try {
				 for (uint32_t j = job->min_j; j < job->max_j; j += VERT_PASS_WIDTH) {
					auto width = std::min<uint32_t>((uint32_t)VERT_PASS_WIDTH,job->max_j - j );
					job->data.memLow   =  (T*)((int32_t*)job->data.mem +   (job->data.cas) * VERT_PASS_WIDTH);
					job->data.memHigh  =  (T*)((int32_t*)job->data.mem + ((!job->data.cas) + 2 * job->data.win_h_0) * VERT_PASS_WIDTH) - 2 * job->data.win_l_0;
					decompressor.interleave_v(&job->data, sa, j, width);
					job->data.memLow   =  job->data.mem - job->data.win_l_0;
					job->data.memHigh  =  job->data.mem + job->data.win_h_0 - 2 * job->data.win_l_0;
					decompressor.decompress_v(&job->data);
					if (!sa->write(j,
								  resWindowRect.y0,
								  j + width,
								  resWindowRect.y1,
								  (int32_t*)(job->data.mem + resWindowRect.y0 - 2 * job->data.win_l_0),
								  1,
								  VERT_PASS_WIDTH,
								  true)) {
						GRK_ERROR("Sparse array write failure");
						job->data.release();
						delete job;
						return 1;
					}
				 }
				job->data.release();
				delete job;
				return 0;
			 } catch (MissingSparseBlockException &msbe){
				  job->data.release();
				  delete job;
				  return 1;
			 }
		};

		//3. calculate synthesis
        horiz.win_l_0 = bandWindowRect[BAND_ORIENT_LL].x0;
        horiz.win_l_1 = bandWindowRect[BAND_ORIENT_LL].x1;
        horiz.win_h_0 = bandWindowRect[BAND_ORIENT_HL].x0;
        horiz.win_h_1 = bandWindowRect[BAND_ORIENT_HL].x1;


        size_t data_size = splitWindowRect[0].width();

		for (uint32_t k = 0; k < 2; ++k) {
			uint32_t num_jobs = (uint32_t)num_threads;
			uint32_t num_rows = splitWindowRect[k].height();
			if (num_rows < num_jobs)
				num_jobs = num_rows;
			uint32_t step_j = num_jobs ? ( num_rows / num_jobs) : 0;
			if (num_threads == 1 ||step_j < HORIZ_PASS_HEIGHT)
				num_jobs = 1;
			std::vector< std::future<int> > results;
			bool blockError = false;
			for(uint32_t j = 0; j < num_jobs; ++j) {
			   auto job = new decompress_job<float, dwt_data<T>>( horiz,splitWindowRect[k].y0 + j * step_j,
													   j < (num_jobs - 1U) ? splitWindowRect[k].y0 + (j + 1U) * step_j : splitWindowRect[k].y1);
				if (!job->data.alloc(data_size,pad)) {
					GRK_ERROR("Out of memory");
					delete job;
					goto cleanup;
				}
				if (num_jobs > 1) {
					results.emplace_back(
						ThreadPool::get()->enqueue([job,executor_h] {
							return executor_h(job);
						})
					);
				} else {
					blockError = ( executor_h(job) != 0);
				}
			}
			for(auto &result: results){
				if (result.get() != 0)
					blockError = true;
			}
			if (blockError)
				goto cleanup;
		}

		data_size = resWindowRect.height() * VERT_PASS_WIDTH;

		vert.win_l_0 = bandWindowRect[BAND_ORIENT_LL].y0;
		vert.win_l_1 = bandWindowRect[BAND_ORIENT_LL].y1;
		vert.win_h_0 = bandWindowRect[BAND_ORIENT_LH].y0;
		vert.win_h_1 = bandWindowRect[BAND_ORIENT_LH].y1;
		uint32_t num_jobs = (uint32_t)num_threads;
		uint32_t num_cols = resWindowRect.width();
		if (num_cols < num_jobs)
			num_jobs = num_cols;
		uint32_t step_j = num_jobs ? ( num_cols / num_jobs) : 0;
		if (num_threads == 1 || step_j < VERT_PASS_WIDTH)
			num_jobs = 1;
		bool blockError = false;
		std::vector< std::future<int> > results;
		for(uint32_t j = 0; j < num_jobs; ++j) {
		   auto job = new decompress_job<float, dwt_data<T>>( vert,resWindowRect.x0 + j * step_j,
												  j < (num_jobs - 1U) ?  resWindowRect.x0 + (j + 1U) * step_j : resWindowRect.x1);
			if (!job->data.alloc(data_size,pad)) {
				GRK_ERROR("Out of memory");
				delete job;
				goto cleanup;
			}
			if (num_jobs > 1) {
				results.emplace_back(
					ThreadPool::get()->enqueue([job,executor_v] {
						return executor_v(job);
					})
				);
			} else {
				blockError = ( executor_v(job) != 0);
			}
		}
		for(auto &result: results){
			if (result.get() != 0)
				blockError = true;
		}
		if (blockError)
			goto cleanup;
    }
    //final read into tile buffer
	bool ret = sa->read(synthesisWindow,
					   tilec->getBuffer()->getWindow()->data,
					   1,
					   tilec->getBuffer()->getWindow()->stride,
					   true);
	assert(ret);
	GRK_UNUSED(ret);
	} catch (MissingSparseBlockException &ex){
		goto cleanup;
	}
	rc = true;

cleanup:
    return rc;
}

bool WaveletReverse::decompress(TileProcessor *p_tcd,
						TileComponent* tilec,
						grk_rect_u32 window,
                        uint32_t numres,
						uint8_t qmfbid){

	if (qmfbid == 1) {
	    if (p_tcd->wholeTileDecompress)
	        return decompress_tile_53(tilec,numres);
	    else
	        return decompress_partial_tile<int32_t,
										getFilterWidth<uint32_t>(true),
										Partial53<int32_t>>(tilec,
															window,
															numres,
															tilec->getSparseBuffer());
	} else {
		 if (p_tcd->wholeTileDecompress)
		        return decompress_tile_97(tilec, numres);
		    else
		        return decompress_partial_tile<vec4f,
											getFilterWidth<uint32_t>(false),
											Partial97<vec4f>>(tilec,
															window,
															numres,
															tilec->getSparseBuffer());
	}
}

}
