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

#pragma once

#include <thread>

namespace grk {

class T1CompressScheduler {
public:
	T1CompressScheduler(TileCodingParams *tcp, grk_tile *tile, uint32_t encodeMaxCblkW,
			uint32_t encodeMaxCblkH, bool needsRateControl);
	~T1CompressScheduler();
	void compress(std::vector<CompressBlockInfo*> *blocks);

private:
	bool compress(size_t threadId, uint64_t maxBlocks);
	void compress(T1Interface *impl, CompressBlockInfo *block);

	grk_tile *tile;
	std::vector<T1Interface*> t1Implementations;
	mutable std::mutex distortion_mutex;
	bool needsRateControl;
	mutable std::mutex block_mutex;
	CompressBlockInfo** encodeBlocks;
	std::atomic<int64_t> blockCount;

};

}