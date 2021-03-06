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


#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "XpressDictionary.h"
#include "Bitstream.h"
#include "HuffmanEncoder.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define PRINT_ERROR(...) // TODO: remove

////////////////////////////// General Definitions and Functions ///////////////////////////////////
#define CHUNK_SIZE		0x10000

#define STREAM_END		0x100
#define STREAM_END_LEN_1	1

#define SYMBOLS			0x200
#define HALF_SYMBOLS	0x100

#define MIN_DATA		HALF_SYMBOLS + 4 // the 512 Huffman lens + 2 uint16s for minimal bitstream

size_t xpress_huff_max_compressed_size(size_t in_len) { return in_len + 34 + (HALF_SYMBOLS + 2) + (HALF_SYMBOLS + 2) * (in_len / CHUNK_SIZE); }


////////////////////////////// Compression Functions ///////////////////////////////////////////////
static size_t xh_compress_lz77(const uint8_t* in, int32_t /* * */ in_len, const uint8_t* in_end, uint8_t* out, uint32_t symbol_counts[SYMBOLS], XpressDictionary* d)
{
	int32_t rem = /* * */ in_len;
	uint32_t mask;
	const const uint8_t* in_orig = in, *out_orig = out;
	uint32_t* mask_out;
	uint8_t i;

	Fill(d, in);
	memset(symbol_counts, 0, SYMBOLS*sizeof(uint32_t));

	////////// Count the symbols and write the initial LZ77 compressed data //////////
	// A uint32 mask holds the status of each subsequent byte (0 for literal, 1 for match)
	// Literals are stored using a single byte for their value
	// Matches are stored in the following manner:
	//   Symbol: a byte (doesn't include the 0x100)
	//   Offset: a uint16 (doesn't include the highest set bit)
	//   Length: for length-3:
	//     0x0000 <= length <  0x0000000F  nothing (contained in symbol)
	//     0x000F <= length <  0x0000010E  length-3-0xF as byte
	//     0x010E <= length <= 0x0000FFFF  0xFF + length-3 as uint16
	//     0xFFFF <  length <= 0xFFFFFFFF  0xFF + 0x0000 + length-3 as uint32
	// The number of bytes between uint32 masks is >=32 and <=192 (6*32)
	//   with the exception that the a length > 0x10002 could be found, but this is longer than a chunk and would immediately end the chunk
	//   if it is the last one, then we need 4 additional bytes, but we don't have to take it into account in any other way
	// The number of represented bytes between uint32 masks is at least the number of actual bytes between them
	while (rem > 0)
	{
		mask = 0;
		mask_out = (uint32_t*)out;
		out += 4;

		// Go through each bit
		for (i = 0; i < 32 && rem > 0; ++i)
		{
			uint32_t len, off;
			mask >>= 1;
			//d->Add(in);
			if (rem >= 3 && (len = Find(d, in, &off)) >= 3)
			{
				// TODO: allow len > rem (chunk-spanning matches)
				if (len > (uint32_t)rem) { len = rem; }
				in += len; rem -= len;
				
				//d->Add(in + 1, len - 1);

				// Create the symbol
				len -= 3;
				mask |= 0x80000000; // set the highest bit
				const uint8_t off_bits = (uint8_t)log2((uint16_t)(off|1)); // |1 prevents taking the log2 of 0 (undefined) and makes 0 -> 1 which is what we want
				const uint8_t sym = (off_bits << 4) | (uint8_t)MIN(0xF, len);
				++symbol_counts[0x100 | sym];
				off ^= 1 << off_bits; // clear highest bit

				// Write symbol / offset / length
				*out = sym; SET_UINT16_RAW(out+1, off); out += 3;
				if (len > 0xFFFF) { *out = 0xFF; SET_UINT16_RAW(out+1, 0); SET_UINT32_RAW(out+3, len); out += 7; }
				else if (len >= 0xFF + 0xF) { *out = 0xFF; SET_UINT16_RAW(out+1, len); out += 3; }
				else if (len >= 0xF)        { *out++ = (uint8_t)(len - 0xF); }
			}
			else
			{
				// Write the literal value (which is the symbol)
				++symbol_counts[*out++ = *in++];
				--rem;
			}
		}

		// Save mask
		SET_UINT32_RAW(mask_out, mask);
	}
	
	// Set the total number of bytes read from in
	/* *in_len -= rem; */
	mask >>= (32-i); // finish moving the value over
	if (in_orig+ /* * */ in_len == in_end)
	{
		// Add the end of stream symbol
		if (i == 32)
		{
			// Need to add a new mask since the old one is full with just one bit set
			SET_UINT32_RAW(out, 1);
			out += 4;
		}
		else
		{
			// Add to the old mask
			mask |= 1 << i; // set the highest bit
		}
		SET_UINT32_RAW(out, 0);
		out += 3;
		++symbol_counts[STREAM_END];
	}
	SET_UINT32_RAW(mask_out, mask);

	// Return the number of bytes in the output
	return out - out_orig;
}

static size_t xh_compress_no_matching(const uint8_t* in, size_t in_len, int is_end, uint8_t* out, uint32_t symbol_counts[SYMBOLS])
{
	const const uint8_t* in_end = in + in_len, *in_endx = in_end - 32;
	const const uint8_t* out_orig = out;
	memset(symbol_counts, 0, SYMBOLS*sizeof(uint32_t));
	while (in < in_endx)
	{
		SET_UINT32_RAW(out, 0); out += 4;
		memcpy(out, in, 32); out += 32;
		for (uint_fast8_t i = 0; i < 32; ++i) { ++symbol_counts[*in++]; }
	}
	const size_t rem = in_end - in; // 1 - 32
	SET_UINT32_RAW(out, 0); out += 4;
	memcpy(out, in, rem); out += rem;
	for (uint_fast8_t i = 0; in < in_end; ++i) { ++symbol_counts[*in++]; }
	if (is_end)
	{
		// Add the end of stream symbol
		if (rem == 32) { SET_UINT32_RAW(out, 1); out += 4; }
		else { const uint32_t mask = 1 << rem; SET_UINT32_RAW(out - rem - 4, mask); }
		SET_UINT32_RAW(out, 0);
		out += 3;
		++symbol_counts[STREAM_END];
	}
	return out - out_orig;
}
static size_t xh_calc_compressed_len(const const uint8_t lens[SYMBOLS], const uint32_t symbol_counts[SYMBOLS], const size_t buf_len)
{
	size_t sym_bits = 16; // we always have at least an extra 16-bits of 0s as the "end-of-chunk"
	uint32_t literal_syms = 0, match_syms = 0;
	for (uint_fast16_t i = 0; i < 0x100; ++i) { sym_bits += lens[i] * symbol_counts[i]; literal_syms += symbol_counts[i]; }
	for (uint_fast16_t i = 0x100; i < SYMBOLS; ++i) { sym_bits += (lens[i] + ((i>>4)&0xF)) * symbol_counts[i]; match_syms += symbol_counts[i]; }
	return (sym_bits+15)/16*2 + (buf_len - (literal_syms + match_syms*3 + (literal_syms+match_syms+31)/32*4)); // compressed size of all symbols after accounting for 16-bit alignment and extra bytes
}
static size_t xh_calc_compressed_len_no_matching(const const uint8_t lens[SYMBOLS], const uint32_t symbol_counts[SYMBOLS])
{
	size_t sym_bits = 16;
	for (uint_fast16_t i = 0; i <= 0x100; ++i) { sym_bits += lens[i] * symbol_counts[i]; }
	return (sym_bits+15)/16*2;
}
static void xh_compress_encode(const uint8_t* in, const const uint8_t* in_end, uint8_t* out, HuffmanEncoder *encoder)
{
	// Write the encoded compressed data
	// This involves parsing the LZ77 compressed data and re-writing it with the Huffman codes
	OutputBitstream bstr;
	OutputBitstream_init(&bstr, out);
	while (in < in_end)
	{
		// Handle a fragment
		// Bit mask tells us how to handle the next 32 symbols, go through each bit
		uint_fast16_t i;
		uint32_t mask;
		for (i = 32, mask = GET_UINT32_RAW(in), in += 4; mask && in < in_end; --i, mask >>= 1)
		{
			if (mask & 1) // offset / length symbol
			{
				// Get the LZ77 sym and offset
				const uint8_t sym = *in++;
				const uint_fast16_t off = GET_UINT16_RAW(in); in += 2;

				// Write the Huffman code
				EncodeSymbol(encoder, 0x100 | sym, &bstr);

				// Write extra length bytes
				if ((sym & 0xF) == 0xF)
				{
					const uint8_t len8 = *in++;
					WriteRawByte(&bstr, len8);
					if (len8 == 0xFF)
					{
						const uint16_t len16 = GET_UINT16_RAW(in); in += 2;
						WriteRawUInt16(&bstr, len16);
						if (len16 == 0) { WriteRawUInt32(&bstr, GET_UINT32_RAW(in)); in += 4; }
					}
				}

				// Write offset bits (off already has the high bit cleared)
				WriteBits(&bstr, off, sym >> 4);
			}
			else
			{
				// Write the literal symbol
				EncodeSymbol(encoder, *in++, &bstr);
			}
		}
		// Write the remaining literal symbols
		for (const uint8_t* end = MIN(in+i, in_end); in != end; ++in) { EncodeSymbol(encoder, *in, &bstr); }
	}

	// Write end of stream symbol and return insufficient buffer or the compressed size
	Finish(&bstr); // make sure that the write stream is finished writing
}

int xpress_huff_compress(const uint8_t* in, size_t in_len, uint8_t* out, size_t* _out_len)
{
	if (in_len == 0) { *_out_len = 0; return 0; }

	uint8_t* buf = (uint8_t*)malloc((in_len >= CHUNK_SIZE) ? 0x1200C : ((in_len + 31) / 32 * 36 + 4 + 8)); // for every 32 bytes in "in" we need up to 36 bytes in the temp buffer + maybe an extra uint32 length symbol + up to 7 for the EOS (+1 for alignment)
	if (buf == NULL) { return ENOMEM; }
	
	const uint8_t* out_orig = out;
	const const uint8_t* in_end = in+in_len;
	size_t out_len = *_out_len;
	XpressDictionary d;
	HuffmanEncoder encoder;
	uint32_t symbol_counts[SYMBOLS]; // 4*512 = 2 kb
	XpressDictionary_init(&d, in, in_end);

	// Go through each chunk except the last
	while (in_len > CHUNK_SIZE)
	{
		////////// Perform the initial LZ77 compression //////////
		size_t buf_len = xh_compress_lz77(in, CHUNK_SIZE, in_end, buf, symbol_counts, &d);

		////////// Create the Huffman codes/lens and Calculate the compressed output size //////////
		const uint8_t* lens = CreateCodes(&encoder, symbol_counts);
		size_t comp_len = xh_calc_compressed_len(lens, symbol_counts, buf_len);
		
		////////// Guarantee Max Compression Size //////////
		// This is required to guarantee max compressed size
		// It is very rare that it is used (mainly medium-high uncompressible data)
		if (comp_len > CHUNK_SIZE+2) // + 2 for alignment
		{
			buf_len = xh_compress_no_matching(in, CHUNK_SIZE, 0, buf, symbol_counts);
			lens = CreateCodesSlow(&encoder, symbol_counts);
			comp_len = xh_calc_compressed_len_no_matching(lens, symbol_counts);
		}

		////////// Output Huffman prefix codes as lengths and Encode compressed data //////////
		if (out_len < HALF_SYMBOLS + comp_len) { PRINT_ERROR("Xpress Huffman Compression Error: Insufficient buffer\n"); free(buf); return ENOBUFS; }
		for (const const uint8_t* end = lens + SYMBOLS; lens < end; lens += 2) { *out++ = lens[0] | (lens[1] << 4); }
		xh_compress_encode(buf, buf+buf_len, out, &encoder);
		in += CHUNK_SIZE; in_len -= CHUNK_SIZE;
		out += comp_len; out_len -= HALF_SYMBOLS + comp_len;
	}

	// Do the last chunk
	if (in_len == 0)
	{
		if (out_len < MIN_DATA) { PRINT_ERROR("Xpress Huffman Compression Error: Insufficient buffer\n"); free(buf); return ENOBUFS; }
		memset(out, 0, MIN_DATA);
		out[STREAM_END>>1] = STREAM_END_LEN_1;
		out += MIN_DATA;
	}
	else
	{
		////////// Perform the initial LZ77 compression //////////
		size_t buf_len = xh_compress_lz77(in, (int32_t)in_len, in_end, buf, symbol_counts, &d);

		////////// Create the Huffman codes/lens and Calculate the compressed output size //////////
		const uint8_t* lens = CreateCodes(&encoder, symbol_counts);
		size_t comp_len = xh_calc_compressed_len(lens, symbol_counts, buf_len);
		
		////////// Guarantee Max Compression Size //////////
		// This is required to guarantee max compressed size
		// It is very rare that it is used (mainly medium-high uncompressible data)
		if (comp_len > in_len+36) // +36 for alignment and end of stream (because it causes a different symbol to need 9 bits)
		{
			buf_len = xh_compress_no_matching(in, in_len, 1, buf, symbol_counts);
			lens = CreateCodesSlow(&encoder, symbol_counts);
			comp_len = xh_calc_compressed_len_no_matching(lens, symbol_counts);
		}

		////////// Output Huffman prefix codes as lengths and Encode compressed data //////////
		if (out_len < HALF_SYMBOLS + comp_len) { PRINT_ERROR("Xpress Huffman Compression Error: Insufficient buffer\n"); free(buf); return ENOBUFS; }
		for (const uint8_t* end = lens + SYMBOLS; lens < end; lens += 2) { *out++ = lens[0] | (lens[1] << 4); }
		xh_compress_encode(buf, buf+buf_len, out, &encoder);
		out += comp_len;
	}

	// Cleanup
	free(buf);

	// Return the total number of compressed bytes
	*_out_len = out - out_orig;
	return 0;
}

