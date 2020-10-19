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

#include "grk_includes.h"

namespace grk {

struct DecompressBlockInfo;
class T1Interface;

class T1DecompressScheduler {
public:
	T1DecompressScheduler(TileCodingParams *tcp, uint16_t blockw, uint16_t blockh);
	~T1DecompressScheduler();
	bool decompress(std::vector<DecompressBlockInfo*> *blocks);

private:
	bool decompressBlock(T1Interface *impl, DecompressBlockInfo *block);
	uint16_t codeblock_width, codeblock_height;  //nominal dimensions of block
	std::vector<T1Interface*> t1Implementations;
	std::atomic_bool success;

	DecompressBlockInfo** decodeBlocks;
};

}