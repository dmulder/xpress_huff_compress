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


/////////////////// Dictionary /////////////////////////////////////////////////
// The dictionary system used for Xpress compression.
//
// TODO: ? Most of the compression time is spent in the dictionary - particularly Find and Add.

#ifndef MSCOMP_XPRESS_DICTIONARY_H
#define MSCOMP_XPRESS_DICTIONARY_H

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define MAX_OFFSET              0xFFFF
#define CHUNK_SIZE              0x10000
#define HASH_BITS		15
#define MAX_CHAIN		11
#define NICE_LENGTH		48

typedef struct
{
	// Window properties
	uint32_t WindowSize;
	uint32_t WindowMask;

	// The hashing function, which works progressively
	uint32_t HashSize;
	uint32_t HashMask;
	unsigned HashShift;

	const uint8_t *start, *end, *end2;
	const uint8_t** table;
	const uint8_t** window;
} XpressDictionary;

void XpressDictionary_init(XpressDictionary *ctx, const const uint8_t* start, const const uint8_t* end)
{
	ctx->WindowSize = CHUNK_SIZE << 1;
	ctx->WindowMask = ctx->WindowSize-1;
	ctx->HashSize = 1 << HASH_BITS;
	ctx->HashMask = ctx->HashSize - 1;
	ctx->HashShift = (HASH_BITS+2)/3;
	ctx->table = (const uint8_t**)malloc(ctx->HashSize*sizeof(const uint8_t*));
	ctx->window = (const uint8_t**)malloc(ctx->WindowSize*sizeof(const uint8_t*));

	ctx->start = start;
	ctx->end = end;
	ctx->end2 = end - 2;
	memset(ctx->table, 0, ctx->HashSize*sizeof(const uint8_t*));
}

uint32_t WindowPos(XpressDictionary *ctx, const uint8_t* x) 
{
	return (uint32_t)((x - ctx->start) & ctx->WindowMask);
}

uint_fast16_t HashUpdate(XpressDictionary *ctx, const uint_fast16_t h, const uint8_t c)
{
	return ((h<<ctx->HashShift) ^ c) & ctx->HashMask;
}


#ifdef MSCOMP_WITH_UNALIGNED_ACCESS
static uint32_t GetMatchLength(const uint8_t* a, const uint8_t* b, const const uint8_t* end, const const uint8_t* end4)
#else
static uint32_t GetMatchLength(const uint8_t* a, const uint8_t* b, const const uint8_t* end)
#endif
{
	// like memcmp but tells you the length of the match and optimized
	// assumptions: a < b < end, end4 = end - 4
	const const uint8_t* b_start = b;
	uint8_t a0, b0;
#ifdef MSCOMP_WITH_UNALIGNED_ACCESS
	while (b < end4 && *((uint32_t*)a) == *((uint32_t*)b))
	{
		a += sizeof(uint32_t);
		b += sizeof(uint32_t);
	}
#endif
	do
	{
		a0 = *a++;
		b0 = *b++;
	} while (b < end && a0 == b0);
	return (uint32_t)(b - b_start - 1);
}

const uint8_t* Fill(XpressDictionary *ctx, const uint8_t* data)
{
	// equivalent to Add(data, CHUNK_SIZE)
	if (data >= ctx->end2) { return ctx->end2; }
	uint32_t pos = WindowPos(ctx, data); // either 0x00000 or CHUNK_SIZE
	const const uint8_t* endx = ((data + CHUNK_SIZE) < ctx->end2) ? data + CHUNK_SIZE : ctx->end2;
	uint_fast16_t hash = HashUpdate(ctx, data[0], data[1]);
	while (data < endx)
	{
		hash = HashUpdate(ctx, hash, data[2]);
		ctx->window[pos++] = ctx->table[hash];
		ctx->table[hash] = data++;
	}
	return endx;
}

static void Add1(XpressDictionary *ctx, const uint8_t* data)
{
	if (data < ctx->end2)
	{
		// TODO: could make ctx more efficient by keeping track of the last hash
		uint_fast16_t hash = HashUpdate(ctx, HashUpdate(ctx, data[0], data[1]), data[2]);
		ctx->window[WindowPos(ctx, data)] = ctx->table[hash];
		ctx->table[hash] = data++;
	}
}
	
static void Add2(XpressDictionary *ctx, const uint8_t* data, size_t len)
{
	if (data >= ctx->end2) { return; }
	uint32_t pos = WindowPos(ctx, data);
	const const uint8_t* end = ((data + len) < ctx->end2) ? data + len : ctx->end2;
	uint_fast16_t hash = HashUpdate(ctx, data[0], data[1]);
	while (data < end)
	{
		hash = HashUpdate(ctx, hash, data[2]);
		ctx->window[pos++] = ctx->table[hash];
		ctx->table[hash] = data++;
	}
}

static void Add0(int count, ...)
{
	int i;
	XpressDictionary *ctx;
	const uint8_t* data;
	size_t len;

	va_list v;
	va_start(v, count);

	ctx = va_arg(v, XpressDictionary *);
	data = va_arg(v, const uint8_t*);
	if (count > 2) {
		len = va_arg(v, size_t);
		Add2(ctx, data, len);
	} else {
		Add1(ctx, data);
	}
}

#define Add(...) Add0(COUNT_PARMS(__VA_ARGS__), __VA_ARGS__)

uint32_t Find(XpressDictionary *ctx, const const uint8_t* data, uint32_t* offset)
{
#if PNTR_BITS <= 32
	const const uint8_t* endx = ctx->end; // on 32-bit, + UINT32_MAX will always overflow
#else
	const const uint8_t* endx = ((data + UINT32_MAX) < data || (data + UINT32_MAX) >= ctx->end) ? ctx->end : data + UINT32_MAX; // if overflow or past end use the end
#endif
#ifdef MSCOMP_WITH_UNALIGNED_ACCESS
	const const uint8_t* xend = data - MAX_OFFSET, end4 = endx - 4;
	const uint16_t prefix = *(uint16_t*)data;
#else
	const const uint8_t* xend = data - MAX_OFFSET;
	const uint8_t prefix0 = data[0], prefix1 = data[1];
#endif
	const uint8_t* x;
	uint32_t len = 2, chain_length = MAX_CHAIN;
	for (x = ctx->window[WindowPos(ctx, data)]; chain_length && x >= xend; x = ctx->window[WindowPos(ctx, x)], --chain_length)
	{
#ifdef MSCOMP_WITH_UNALIGNED_ACCESS
		if (*(uint16_t*)x == prefix)
		{
			// at ctx point the at least 3 bytes are matched (due to the hashing function forcing byte 3 to the same)
			const uint32_t l = GetMatchLength(x, data, endx, end4);
#else
		if (x[0] == prefix0 && x[1] == prefix1)
		{
			// at ctx point the at least 3 bytes are matched (due to the hashing function forcing byte 3 to the same)
			const uint32_t l = GetMatchLength(x, data, endx);
#endif
			if (l > len)
			{
				*offset = (uint32_t)(data - x);
				len = l;
				if (len >= NICE_LENGTH) { break; }
			}
		}
	}
	return len;
}

#endif
