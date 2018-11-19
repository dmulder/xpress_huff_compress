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

#ifndef MSCOMP_SORTING
#define MSCOMP_SORTING

#define SORT_SWITCH_TO_INSERT_LIMIT 90

// Insertion-sorts syms[l, r) using conditions[syms[x]]
// Use insertion-sort so that it is stable, keeping symbols in increasing order
// This is only used at the tail end of merge_sort.
#define insertion_sort(type) \
void insertion_sort_##type(uint16_t* syms, const type* const conditions, const uint_fast16_t len) \
{ \
	for (uint_fast16_t i = 1; i < len; ++i) \
	{ \
		const uint16_t x = syms[i]; \
		const type cond = conditions[x]; \
		uint_fast16_t j = i; \
		while (j > 0 && conditions[syms[j-1]] > cond) { syms[j] = syms[j-1]; --j; } \
		syms[j] = x; \
	} \
}
insertion_sort(uint8_t)
insertion_sort(uint32_t)

// Merge-sorts syms[l, r) using conditions[syms[x]]
// Use merge-sort so that it is stable, keeping symbols in increasing order
#define merge_sort(type) \
void merge_sort_##type(uint16_t* syms, uint16_t* temp, const type* const conditions, const uint_fast16_t len) \
{ \
	if (len < SORT_SWITCH_TO_INSERT_LIMIT) \
	{ \
		insertion_sort_##type(syms, conditions, len); \
	} \
	else \
	{ \
		const uint_fast16_t m = len >> 1; \
		uint_fast16_t i = 0, j = 0, k = m; \
		merge_sort_##type(syms,   temp,   conditions, m      ); \
		merge_sort_##type(syms+m, temp+m, conditions, len - m); \
		memcpy(temp, syms, len*sizeof(uint16_t)); \
		while (j < m && k < len) syms[i++] = (conditions[temp[k]] < conditions[temp[j]]) ? temp[k++] : temp[j++]; \
		if (j < m)        { memcpy(syms+i, temp+j, (m  -j)*sizeof(uint16_t)); } \
		else if (k < len) { memcpy(syms+i, temp+k, (len-k)*sizeof(uint16_t)); } \
	} \
}
merge_sort(uint8_t)
merge_sort(uint32_t)

#endif
