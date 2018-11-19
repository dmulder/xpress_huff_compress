// ms-compress: implements Microsoft compression algorithms
// Copyright (C) 2012  Jeffrey Bush  jeff@coderforlife.com
// Copyright (C) 2018 David Mulder <dmulder@suse.com>
//
// This library is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


////////////////////////////// Bitstreams //////////////////////////////////////////////////////////
// A bitstream that allows either reading or writing, but not both at the same time.
// It reads uint16s for bits and 16 bits can be reliably read at a time.
// These are designed for speed and perform few checks. The burden of checking is on the caller.
// See the functions for assumptions they make that should be checked by the caller (asserts check
// these in the functions as well). Note that ctx->bits is >= 16 unless near the very end of the
// stream.

#ifndef MSCOMP_BITSTREAM_H
#define MSCOMP_BITSTREAM_H

#if defined(MSCOMP_WITH_UNALIGNED_ACCESS)
        #define GET_UINT16_RAW(x)               (*(const uint16_t*)(x))
        #define GET_UINT32_RAW(x)               (*(const uint32_t*)(x))
        #define SET_UINT16_RAW(x,val)   (*(uint16_t*)(x) = (uint16_t)(val))
        #define SET_UINT32_RAW(x,val)   (*(uint32_t*)(x) = (uint32_t)(val))
        #if defined(MSCOMP_LITTLE_ENDIAN)
                #define GET_UINT16(x)           GET_UINT16_RAW(x)
                #define GET_UINT32(x)           GET_UINT32_RAW(x)
                #define SET_UINT16(x,val)       SET_UINT16_RAW(x,val)
                #define SET_UINT32(x,val)       SET_UINT32_RAW(x,val)
        #elif defined(MSCOMP_BIG_ENDIAN)
                // These could also use the without-unaligned-access versions always
                #define GET_UINT16(x)           byte_swap(*(const uint16_t*)(x))
                #define GET_UINT32(x)           byte_swap(*(const uint32_t*)(x))
                #define SET_UINT16(x,val)       (*(uint16_t*)(x) = byte_swap((uint16_t)(val)))
                #define SET_UINT32(x,val)       (*(uint32_t*)(x) = byte_swap((uint32_t)(val)))
        #endif
#else // if MSCOMP_WITHOUT_UNALIGNED_ACCESS:
        // When not using unaligned access, nothing needs to be done for different endians
        #define GET_UINT16_RAW(x)               (((uint8_t*)(x))[0]|(((uint8_t*)(x))[1]<<8))
        #define GET_UINT32_RAW(x)               (((uint8_t*)(x))[0]|(((uint8_t*)(x))[1]<<8)|(((uint8_t*)(x))[2]<<16)|(((uint8_t*)(x))[3]<<24))
        #define SET_UINT16_RAW(x,val)   (((uint8_t*)(x))[0]=(uint8_t)(val), ((uint8_t*)(x))[1]=(uint8_t)((val)>>8))
        #define SET_UINT32_RAW(x,val)   (((uint8_t*)(x))[0]=(uint8_t)(val), ((uint8_t*)(x))[1]=(uint8_t)((val)>>8), ((uint8_t*)(x))[2]=(uint8_t)((val)>>16), ((uint8_t*)(x))[3]=(uint8_t)((val)>>24))
        #define GET_UINT16(x)                   GET_UINT16_RAW(x)
        #define GET_UINT32(x)                   GET_UINT32_RAW(x)
        #define SET_UINT16(x,val)               SET_UINT16_RAW(x,val)
        #define SET_UINT32(x,val)               SET_UINT32_RAW(x,val)
#endif

typedef struct
{
	uint8_t* out;
	uint16_t* pntr[2];	// the uint16's to write the data in mask to when there are enough bits
	uint32_t mask;		// The next bits to be read/written in the bitstream
	uint_fast8_t bits;	// The number of bits in mask that are valid
} OutputBitstream;

void OutputBitstream_init(OutputBitstream *ctx, uint8_t* out)
{
	ctx->out = out+4;
	ctx->mask = 0;
	ctx->bits = 0;
	ctx->pntr[0] = (uint16_t*)(out);
	ctx->pntr[1] = (uint16_t*)(out+2);
}

uint8_t* RawStream(OutputBitstream *ctx)
{
	return ctx->out;
}

void WriteBits(OutputBitstream *ctx, uint32_t b, uint_fast8_t n)
{
	ctx->mask |= b << (32 - (ctx->bits += n));
	if (ctx->bits > 16)
	{
		SET_UINT16(ctx->pntr[0], ctx->mask >> 16);
		ctx->mask <<= 16;
		ctx->bits &= 0xF; //ctx->bits -= 16;
		ctx->pntr[0] = ctx->pntr[1];
		ctx->pntr[1] = (uint16_t*)(ctx->out);
		ctx->out += 2;
	}
}

void WriteRawByte(OutputBitstream *ctx, uint8_t x)
{
	*ctx->out++ = x;
}

void WriteRawUInt16(OutputBitstream *ctx, uint16_t x)
{
	SET_UINT16(ctx->out, x);
	ctx->out += 2;
}

void WriteRawUInt32(OutputBitstream *ctx, uint32_t x)
{
	SET_UINT32(ctx->out, x);
	ctx->out += 4;
}

void Finish(OutputBitstream *ctx)
{
	SET_UINT16(ctx->pntr[0], ctx->mask >> 16); // if !bits then mask is 0 anyways
	SET_UINT16_RAW(ctx->pntr[1], 0);
}

#endif
