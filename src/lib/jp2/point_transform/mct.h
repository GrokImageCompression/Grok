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

#pragma once
namespace grk {


class mct {

public:

	/**
	 Apply a reversible multi-component transform to an image
	 @param c0 Samples for red component
	 @param c1 Samples for green component
	 @param c2 Samples blue component
	 @param n Number of samples for each component
	 */
	static void compress_rev(int32_t *c0, int32_t *c1, int32_t *c2, uint64_t n);
	/**
	 Apply a reversible multi-component inverse transform to an image
	 @param tile tile
	 @param image image
	 @param tccps tile component coding parameters
	 */
	static void decompress_rev(Tile *tile, GrkImage *image,
			TileComponentCodingParams *tccps);

	/**
	 Get wavelet norms for reversible transform
	 */
	static const double* get_norms_rev(void);


	/**
	 Apply an irreversible multi-component transform to an image
	 @param c0 Samples for red component
	 @param c1 Samples for green component
	 @param c2 Samples blue component
	 @param n Number of samples for each component
	 */
	static void compress_irrev(int32_t *c0, int32_t *c1, int32_t *c2, uint64_t n);
	/**
	 Apply an irreversible multi-component inverse transform to an image
	 @param tile tile
	 @param image image
	 @param tccps tile component coding parameters
	 */
	static void decompress_irrev(Tile *tile, GrkImage *image,
			TileComponentCodingParams *tccps);

	/**
	 Get wavelet norms for irreversible transform
	 */
	static const double* get_norms_irrev(void);

	/**
	 Custom MCT transform
	 @param p_coding_data    MCT data
	 @param n                size of components
	 @param p_data           components
	 @param nb_comp          nb of components (i.e. size of p_data)
	 @param is_signed        indicates if the data is signed
	 @return false if function encounter a problem, true otherwise
	 */
	static bool compress_custom(uint8_t *p_coding_data, uint64_t n, uint8_t **p_data,
			uint32_t nb_comp, uint32_t is_signed);
	/**
	 Custom MCT decode
	 @param pDecodingData    MCT data
	 @param n                size of components
	 @param pData            components
	 @param pNbComp          nb of components (i.e. size of p_data)
	 @param isSigned         tells if the data is signed
	 @return false if function encounter a problem, true otherwise
	 */
	static bool decompress_custom(uint8_t *pDecodingData, uint64_t n, uint8_t **pData,
			uint32_t pNbComp, uint32_t isSigned);
	/**
	 Calculate norm of MCT transform
	 @param pNorms         MCT data
	 @param nb_comps       number of components
	 @param pMatrix        components
	 */
	static void calculate_norms(double *pNorms, uint32_t nb_comps, float *pMatrix);

	/**
	 Apply a reversible inverse dc shift to an image
	 @param tile tile
	 @param image image
	 @param tccps tile component coding parameters
	 */
	static void decompress_dc_shift_rev(Tile *tile, GrkImage *image,TileComponentCodingParams *tccps, uint32_t compno);

	/**
	 Apply an irreversible inverse dc shift to an image
	 @param tile tile
	 @param image image
	 @param tccps tile component coding parameters
	 */
	static void decompress_dc_shift_irrev(Tile *tile, GrkImage *image,TileComponentCodingParams *tccps, uint32_t compno);

};

/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

}
