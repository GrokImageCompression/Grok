/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 */
#include "testing.h"
#include "T1Part1.h"
#include "mqc.h"
#include "t1_decode.h"
#include "t1_decode_opt.h"
#include "t1_encode.h"

namespace grk {
namespace t1_part1{

T1Part1::T1Part1(bool isEncoder, grk_tcp *tcp, uint16_t maxCblkW,
		uint16_t maxCblkH) :
		t1_decoder(nullptr), t1_encoder(nullptr) {
	if (isEncoder) {
		t1_encoder = new t1_encode();
		if (!t1_encoder->allocateBuffers(maxCblkW, maxCblkH)) {
			throw std::exception();
		}
	} else {
		grk_tccp *tccp = &tcp->tccps[0];
		if (!tccp->cblk_sty)
			t1_decoder = new t1_decode_opt(maxCblkW, maxCblkH);
		else
			t1_decoder = new t1_decode(maxCblkW, maxCblkH);
	}
}
T1Part1::~T1Part1() {
	delete t1_decoder;
	delete t1_encoder;
}
void T1Part1::preEncode(encodeBlockInfo *block, grk_tcd_tile *tile,
		uint32_t &max) {
	t1_encoder->preEncode(block, tile, max);
}
double T1Part1::encode(encodeBlockInfo *block, grk_tcd_tile *tile, uint32_t max,
		bool doRateControl) {
	double dist = t1_encoder->encode_cblk(block->cblk, (uint8_t) block->bandno,
			block->compno,
			(tile->comps + block->compno)->numresolutions - 1 - block->resno,
			block->qmfbid, block->stepsize, block->cblk_sty, tile->numcomps,
			block->mct_norms, block->mct_numcomps, max, doRateControl);
#ifdef DEBUG_LOSSLESS_T1
		t1_decode* t1Decode = new t1_decode(t1_encoder->w, t1_encoder->h);

		grk_tcd_cblk_dec* cblkDecode = new grk_tcd_cblk_dec();
		cblkDecode->data = nullptr;
		cblkDecode->segs = nullptr;
		if (!cblkDecode->alloc()) {
			return dist;
		}
		cblkDecode->x0 = block->cblk->x0;
		cblkDecode->x1 = block->cblk->x1;
		cblkDecode->y0 = block->cblk->y0;
		cblkDecode->y1 = block->cblk->y1;
		cblkDecode->numbps = block->cblk->numbps;
		cblkDecode->numSegments = 1;
		memset(cblkDecode->segs, 0, sizeof(grk_tcd_seg));
		auto seg = cblkDecode->segs;
		seg->numpasses = block->cblk->num_passes_encoded;
		auto rate = seg->numpasses ? block->cblk->passes[seg->numpasses - 1].rate : 0;
		seg->len = rate;
		seg->dataindex = 0;
		min_buf_vec_push_back(&cblkDecode->seg_buffers, block->cblk->data, (uint16_t)rate);
		//decode
		t1Decode->decode_cblk(cblkDecode, block->bandno, 0, 0);

		//compare
		auto index = 0;
		for (uint32_t j = 0; j < t1_encoder->h; ++j) {
			for (uint32_t i = 0; i < t1_encoder->w; ++i) {
				auto valBefore = block->unencodedData[index];
				auto valAfter = t1Decode->data[index] / 2;
				if (valAfter != valBefore) {
					printf("T1 encode @ block location (%d,%d); original data=%x, round trip data=%x\n", i, j, valBefore, valAfter);
				}
				index++;
			}
		}

		delete t1Decode;
		// the next line triggers an exception, so commented out at the moment
		//grok_free(cblkDecode->segs);
		delete cblkDecode;
		delete[] block->unencodedData;
		block->unencodedData = nullptr;
#endif
	return dist;
}
bool T1Part1::decode(decodeBlockInfo *block) {
	return t1_decoder->decode_cblk(block->cblk, (uint8_t) block->bandno,
			block->cblk_sty);
}

void T1Part1::postDecode(decodeBlockInfo *block) {
	t1_decoder->postDecode(block);
}
}
}
