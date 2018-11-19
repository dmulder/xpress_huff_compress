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

#ifndef MSCOMP_HUFFMAN_ENCODER
#define MSCOMP_HUFFMAN_ENCODER

#include "Bitstream.h"
#include "sorting.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define HUFF_BITS_MAX   15
#define SYMBOLS                 0x200

typedef struct
{
	uint16_t codes[SYMBOLS];
	uint8_t lens[SYMBOLS];
} HuffmanEncoder;

#define HEAP_PUSH(x)                         \
{                                            \
	heap[++heap_len] = x;                    \
	uint_fast16_t j = heap_len;              \
	while (weights[x] < weights[heap[j>>1]]) \
	{                                        \
		heap[j] = heap[j>>1]; j >>= 1;       \
	}                                        \
	heap[j] = x;                             \
}

#define HEAP_POP()                                  \
{                                                   \
	uint_fast16_t i = 1, t = heap[1] = heap[heap_len--]; \
	for (;;)                                        \
	{                                               \
		uint_fast16_t j = i << 1;                   \
		if (j > heap_len) { break; }                \
		if (j < heap_len && weights[heap[j+1]] < weights[heap[j]]) { ++j; } \
		if (weights[t] < weights[heap[j]]) { break; } \
		heap[i] = heap[j];                          \
		i = j;                                      \
	}                                               \
	heap[i] = t;                                    \
}

const uint8_t* CreateCodes(HuffmanEncoder *ctx, uint32_t symbol_counts[SYMBOLS]) // 17 kb stack (for SYMBOLS == 0x200)
{
	// Creates Length-Limited Huffman Codes using an optimized version of the original Huffman algorithm
	// Does not always produce optimal codes
	// Algorithm from "In-Place Calculation of Minimum-Redundancy Codes" by A Moffat and J Katajainen
	// Code adapted from bzip2. See http://www.bzip.org/.
	memset(ctx->codes, 0, sizeof(ctx->codes));

	// Compute the initial weights (the weight is in the upper 24 bits, the depth (initially 0) is in the lower 8 bits
	uint32_t weights[SYMBOLS * 2]; // weights of nodes
	weights[0] = 0;
	for (uint_fast16_t i = 0; i < SYMBOLS; ++i) { weights[i+1] = (symbol_counts[i] == 0 ? 1 : symbol_counts[i]) << 8; }

	for (;;)
	{
		// Build the initial heap
		uint_fast16_t heap_len = 0, heap[SYMBOLS + 2] = { 0 }; // heap of symbols, 1 to heap_len
		for (uint_fast16_t i = 1; i <= SYMBOLS; ++i) { HEAP_PUSH(i); }

		// Build the tree (its a bottom-up tree)
		uint_fast16_t n_nodes = SYMBOLS, parents[SYMBOLS * 2]; // parents of nodes, 1 to n_nodes
		memset(parents, 0, sizeof(parents));
		while (heap_len > 1)
		{
			uint_fast16_t n1 = heap[1]; HEAP_POP();
			uint_fast16_t n2 = heap[1]; HEAP_POP();
			parents[n1] = parents[n2] = ++n_nodes;
			weights[n_nodes] = ((weights[n1]&0xffffff00)+(weights[n2]&0xffffff00)) | (1 + MAX((weights[n1]&0x000000ff),(weights[n2]&0x000000ff)));
			HEAP_PUSH(n_nodes);
		}

		// Create the actual length codes
		int too_long = 0;
		for (uint_fast16_t i = 1; i <= SYMBOLS; ++i)
		{
			uint8_t j = 0;
			uint_fast16_t k = i;
			while (parents[k] > 0) { k = parents[k]; ++j; }
			ctx->lens[i-1] = j;
			if (j > HUFF_BITS_MAX) { too_long = 1; }
		}

		// If we had codes that were too long then we need to make all the weights smaller
		if (!too_long) { break; }
		for (uint_fast16_t i = 1; i <= SYMBOLS; ++i)
		{
			weights[i] = (1 + (weights[i] >> 9)) << 8;
		}
	}

	// Compute the values of the codes
	uint_fast16_t min = ctx->lens[0], max = min;
	for (uint_fast16_t i = 1; i < SYMBOLS; ++i)
	{
		if (ctx->lens[i] > max) { max = ctx->lens[i]; }
		else if (ctx->lens[i] < min) { min = ctx->lens[i]; }
	}
	uint16_t code = 0;
	for (uint_fast16_t n = min; n <= max; ++n)
	{
		for (uint_fast16_t i = 0; i < SYMBOLS; ++i)
		{
			if (ctx->lens[i] == n) { ctx->codes[i] = code++; }
		}
		code <<= 1;
	}

	// Done!
	return ctx->lens;
}

const uint8_t* CreateCodesSlow(HuffmanEncoder *ctx, uint32_t symbol_counts[SYMBOLS]) // 3 kb stack (for SYMBOLS == 0x200) [519kb stack when compiled with MSCOMP_WITH_LARGE_STACK]
{
	// Creates Length-Limited Huffman Codes using the package-merge algorithm
	// Always produces optimal codes but is significantly slower than the Huffman algorithm
	memset(ctx->codes, 0, sizeof(ctx->codes));
	memset(ctx->lens,  0, sizeof(ctx->lens));

	// Fill the syms_by_count and syms_by_length with the symbols that were found
	uint16_t syms_by_count[SYMBOLS], syms_by_len[SYMBOLS], temp[SYMBOLS]; // 3*2*512 = 3 kb
	uint_fast16_t len = 0;
	for (uint_fast16_t i = 0; i < SYMBOLS; ++i) { if (symbol_counts[i]) { syms_by_count[len] = (uint16_t)i; syms_by_len[len++] = (uint16_t)i; ctx->lens[i] = HUFF_BITS_MAX; } }

	////////// Get the Huffman lengths //////////
	merge_sort_uint32_t(syms_by_count, temp, symbol_counts, len); // sort by the counts
	if (len == 1)
	{
		ctx->lens[syms_by_count[0]] = 1; // never going to happen, but the code below would probably assign a length of 0 which is not right
	}
	else
	{
		///// Package-Merge Algorithm /////
		typedef struct _collection // 516 bytes each
		{
			uint8_t symbols[SYMBOLS];
			uint_fast16_t count;
		} collection;
#ifdef MSCOMP_WITH_LARGE_STACK
		collection _cols[SYMBOLS], _next_cols[SYMBOLS],
#else
		collection *_cols = (collection*)malloc(SYMBOLS*sizeof(collection)),
			*_next_cols = (collection*)malloc(SYMBOLS*sizeof(collection)),
#endif
			*cols = _cols, *next_cols = _next_cols; // 2*516*512 = 516 kb (not on stack any more)
		uint_fast16_t cols_len = 0, next_cols_len = 0;

		// Start at the lowest value row, adding new collection
		for (uint_fast16_t j = 0; j < HUFF_BITS_MAX; ++j)
		{
			uint_fast16_t cols_pos = 0, pos = 0;

			// All but the last one/none get added to collections
			while ((cols_len-cols_pos + len-pos) > 1)
			{
				memset(next_cols+next_cols_len, 0, sizeof(collection));
				for (uint_fast16_t i = 0; i < 2; ++i) // hopefully unrolled...
				{
					if (pos >= len || (cols_pos < cols_len && cols[cols_pos].count < symbol_counts[syms_by_count[pos]]))
					{
						// Add cols[cols_pos]
						next_cols[next_cols_len].count += cols[cols_pos].count;
						for (uint_fast16_t s = 0; s < SYMBOLS; ++s)
						{
							next_cols[next_cols_len].symbols[s] += cols[cols_pos].symbols[s];
						}
						++cols_pos;
					}
					else
					{
						// Add syms[pos]
						next_cols[next_cols_len].count += symbol_counts[syms_by_count[pos]];
						++next_cols[next_cols_len].symbols[syms_by_count[pos]];
						++pos;
					}
				}
				++next_cols_len;
			}
		
			// Leftover gets dropped
			if (cols_pos < cols_len)
			{
				const uint8_t* const syms = cols[cols_pos].symbols;
				for (uint_fast16_t i = 0; i < SYMBOLS; ++i) { ctx->lens[i] -= syms[i]; }
			}
			else if (pos < len)
			{
				--ctx->lens[syms_by_count[pos]];
			}

			// Move the next_collections to the current collections
			collection* temp_cols = cols; cols = next_cols; next_cols = temp_cols;
			cols_len = next_cols_len;
			next_cols_len = 0;
		}
#ifndef MSCOMP_WITH_LARGE_STACK
		free(_cols); free(_next_cols);
#endif

		////////// Create Huffman codes from lengths //////////
		merge_sort_uint8_t(syms_by_len, temp, ctx->lens, len); // Sort by the code lengths
		for (uint_fast16_t i = 1; i < len; ++i)
		{
			// Code is previous code +1 with added zeroes for increased code length
			ctx->codes[syms_by_len[i]] = (ctx->codes[syms_by_len[i-1]] + 1) << (ctx->lens[syms_by_len[i]] - ctx->lens[syms_by_len[i-1]]);
		}
	}

	return ctx->lens;
}

void EncodeSymbol(HuffmanEncoder *ctx, uint_fast16_t sym, OutputBitstream *bits)
{
	WriteBits(bits, ctx->codes[sym], ctx->lens[sym]);
}

#undef HEAP_PUSH
#undef HEAP_POP

#endif
