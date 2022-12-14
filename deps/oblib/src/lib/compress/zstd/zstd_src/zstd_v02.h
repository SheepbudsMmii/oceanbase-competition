/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef ZSTD_V02_H_4174539423
#define ZSTD_V02_H_4174539423

#if defined (__cplusplus)
extern "C" {
#endif

/* *************************************
*  Includes
***************************************/
#include <stddef.h>   /* size_t */


/* *************************************
*  Simple one-step function
***************************************/
/**
ZSTDv02_decompress() : decompress ZSTD frames compliant with v0.2.x format
    compressedSize : is the exact source size
    maxOriginalSize : is the size of the 'dst' buffer, which must be already allocated.
                      It must be equal or larger than originalSize, otherwise decompression will fail.
    return : the number of bytes decompressed into destination buffer (originalSize)
             or an errorCode if it fails (which can be tested using ZSTDv01_isError())
*/
size_t ZSTDv02_decompress( void* dst, size_t maxOriginalSize,
                     const void* src, size_t compressedSize);

/**
ZSTDv02_isError() : tells if the result of ZSTDv02_decompress() is an error
*/
unsigned ZSTDv02_isError(size_t code);


/* *************************************
*  Advanced functions
***************************************/
typedef struct ZSTDv02_Dctx_s ZSTDv02_Dctx;
ZSTDv02_Dctx* ZSTDv02_createDCtx(void);
size_t ZSTDv02_freeDCtx(ZSTDv02_Dctx* dctx);

size_t ZSTDv02_decompressDCtx(void* ctx,
                              void* dst, size_t maxOriginalSize,
                        const void* src, size_t compressedSize);

/* *************************************
*  Streaming functions
***************************************/
size_t ZSTDv02_resetDCtx(ZSTDv02_Dctx* dctx);

size_t ZSTDv02_nextSrcSizeToDecompress(ZSTDv02_Dctx* dctx);
size_t ZSTDv02_decompressContinue(ZSTDv02_Dctx* dctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize);
/**
  Use above functions alternatively.
  ZSTD_nextSrcSizeToDecompress() tells how much bytes to provide as 'srcSize' to ZSTD_decompressContinue().
  ZSTD_decompressContinue() will use previous data blocks to improve compression if they are located prior to current block.
  Result is the number of bytes regenerated within 'dst'.
  It can be zero, which is not an error; it just means ZSTD_decompressContinue() has decoded some header.
*/

/* *************************************
*  Prefix - version detection
***************************************/
#define ZSTDv02_magicNumber 0xFD2FB522   /* v0.2 */


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD_V02_H_4174539423 */
