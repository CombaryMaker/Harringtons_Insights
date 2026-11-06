/**
 * @file lzav_hi.h
 *
 * @brief lzav-hi: a stronger ENCODER for the unmodified LZAV stream format
 * (data format 4). The output is decoded by the stock lzav_decompress() from
 * LZAV's own lzav.h, unchanged.
 *
 * Method (all classic, public LZ77 techniques; nothing format-changing):
 *  - binary-tree match finder over the whole input (finds the longest match
 *    and the nearest distance for every shorter length);
 *  - optimal (shortest-path) parsing with LZAV's exact block costs, where the
 *    DP state includes LZAV's offset-carry shift (csh) so that the cost of the
 *    next reference block is exact;
 *  - the minimum reference length (mref, stored in the stream's prefix byte)
 *    is chosen per input (4 or 5), both of which the stock decoder accepts;
 *  - every block is written by LZAV's own lzav_write_blk_4() and
 *    lzav_write_fin_4(); only the choice of blocks differs from stock.
 *
 * Include "lzav.h" (stock, unmodified) before this file.
 *
 * MIT License
 *
 * Copyright (c) 2026 Michael Hugh Harrington
 *
 * LZAV itself: Copyright (c) 2023-2026 Aleksey Vaneev, MIT License.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef LZAV_HI_INCLUDED
#define LZAV_HI_INCLUDED

#if !defined( LZAV_INCLUDED )
	#error "Include the stock lzav.h before lzav_hi.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined( __cplusplus )
	#if defined( LZAV_NS_CUSTOM )
		using namespace LZAV_NS_CUSTOM;
	#else
		using namespace lzav; // Stock lzav.h's default C++ namespace.
	#endif
#endif

typedef struct
{
	int mref;      ///< 4, 5, or 0 = try both and keep the smaller.
	int nice_len;  ///< Match length that is taken immediately (forced).
	int depth;     ///< Binary-tree search depth (candidates per position).
	int chunk;     ///< Positions per optimal-parse chunk (soft limit).
	int full_len;  ///< Every length up to this is priced; above it, only
	               ///< each candidate's maximum length (0 = price all).
	int one_state; ///< 1 = keep one DP node per position (faster, approx.);
	               ///< 0 = one node per offset-carry state (exact costs).
	int blk_cost;  ///< Extra price per reference block in 1/64 byte (a
	               ///< decode-speed term: fewer, longer blocks); 0 = 1.
} lzavhi_params;

/** Default (max) level: deep binary-tree search, exact carry-state DP,
 * mref 4. */
static const lzavhi_params lzavhi_default_params =
	{ 4, 512, 48, 16384, 32, 0 };

/** Same search as the default, but mref 5 (the value stock HI writes). */
static const lzavhi_params lzavhi_m5_params = { 5, 512, 48, 16384, 32, 0 };

/** Faster preset: shallow search, one DP node per position, mref 4. */
static const lzavhi_params lzavhi_fast_params = { 4, 128, 8, 8192, 24, 1 };

#define LZAVHI_NIL 0xFFFFFFFFu
#define LZAVHI_INF 0xFFFFFFFFu
#define LZAVHI_NS 5 // Number of offset-carry states.

static const int lzavhi_cs[ LZAVHI_NS ] = { 0, 2, 3, 5, 6 };
static const int lzavhi_cs_idx[ 7 ] = { 0, -1, 1, 2, -1, 3, 4 };

typedef struct
{
	uint32_t cost; // Scaled cost (bytes * 64 + 1 per reference block).
	uint32_t lc; // Pending literal count at this node.
	uint32_t from; // Predecessor position (chunk-relative).
	uint32_t len; // 0 = arrived by a literal; else match length.
	uint32_t dist; // Match distance.
	uint8_t froms; // Predecessor slot.
	uint8_t st; // Offset-carry state index at this node.
} lzavhi_node;

typedef struct
{
	uint32_t len, dist;
} lzavhi_match;

/* ---------------- Binary-tree match finder ---------------- */

typedef struct
{
	const uint8_t* src;
	uint32_t n; // Number of positions that may be inserted.
	uint32_t* head;
	uint32_t* son;
	uint32_t hbits;
	uint32_t depth;
} lzavhi_mf;

static inline uint32_t lzavhi_h4( const uint8_t* p, const uint32_t hbits )
{
	uint32_t v;
	memcpy( &v, p, 4 );
	return( (uint32_t) ( v * 2654435761u ) >> ( 32 - hbits ));
}

static inline uint32_t lzavhi_cmp( const uint8_t* a, const uint8_t* b,
	uint32_t l, const uint32_t lim )
{
	while( l + 8 <= lim )
	{
		uint64_t x, y;
		memcpy( &x, a + l, 8 );
		memcpy( &y, b + l, 8 );
		x ^= y;

		if( x != 0 )
		{
			return( l + ( (uint32_t) __builtin_ctzll( x ) >> 3 ));
		}

		l += 8;
	}

	while( l < lim && a[ l ] == b[ l ])
	{
		l++;
	}

	return( l );
}

/**
 * Inserts position `pos` into the tree and (if `out` is non-null) collects
 * strictly-improving matches of length >= minlen. Returns the match count.
 * `lim` is the maximum match length to search for.
 */

static uint32_t lzavhi_bt( lzavhi_mf* const mf, const uint32_t pos,
	const uint32_t lim, const uint32_t minlen, lzavhi_match* const out )
{
	const uint8_t* const cur = mf -> src + pos;
	const uint32_t h = lzavhi_h4( cur, mf -> hbits );
	uint32_t cm = mf -> head[ h ];
	mf -> head[ h ] = pos;

	uint32_t* ptr0 = mf -> son + 2 * (size_t) pos + 1; // "Greater" slot.
	uint32_t* ptr1 = mf -> son + 2 * (size_t) pos; // "Smaller" slot.
	uint32_t len0 = 0, len1 = 0;
	uint32_t best = minlen - 1;
	uint32_t cnt = mf -> depth;
	uint32_t nm = 0;

	while( 1 )
	{
		if( cm == LZAVHI_NIL || cnt-- == 0 )
		{
			*ptr0 = *ptr1 = LZAVHI_NIL;
			break;
		}

		const uint8_t* const pb = mf -> src + cm;
		uint32_t len = ( len0 < len1 ? len0 : len1 );
		len = lzavhi_cmp( cur, pb, len, lim );

		if( len > best )
		{
			best = len;

			if( out != NULL )
			{
				out[ nm ].len = len;
				out[ nm ].dist = pos - cm;
				nm++;
			}
		}

		uint32_t* const pair = mf -> son + 2 * (size_t) cm;

		if( len >= lim )
		{
			*ptr1 = pair[ 0 ];
			*ptr0 = pair[ 1 ];
			break;
		}

		if( pb[ len ] < cur[ len ])
		{
			*ptr1 = cm;
			ptr1 = pair + 1;
			cm = *ptr1;
			len1 = len;
		}
		else
		{
			*ptr0 = cm;
			ptr0 = pair;
			cm = *ptr0;
			len0 = len;
		}
	}

	return( nm );
}

/* ---------------- LZAV format-4 exact block costs ---------------- */

static inline uint32_t lzavhi_lit_hdr( const uint32_t lc )
{
	if( lc == 0 ) return( 0 );
	if( lc < 16 ) return( 1 );
	if( lc < 16 + 255 ) return( 2 );
	if( lc - 16 < 32768 ) return( 4 );
	return( 6 );
}

static inline uint32_t lzavhi_len_ext( const uint32_t v ) // v = rc - mref1
{
	if( v < 16 ) return( 0 );
	if( v < 16 + 255 ) return( 1 );
	if( v - 16 < 32768 ) return( 3 );
	return( 5 );
}

/**
 * Size of a reference block header+offset (without length extension), and
 * the resulting carry-state index; mirrors lzav_write_blk_4(). Returns 0 if
 * the distance is not encodable in this state.
 */

static inline uint32_t lzavhi_ref_cost( uint32_t d, const int lcflag,
	const int s, int* const ns )
{
	const int csh = lzavhi_cs[ s ];
	const uint32_t sh = (uint32_t) csh + ( lcflag ? 2 : 0 );

	if( (uint64_t) d >= ( (uint64_t) LZAV_WIN_LEN << sh ))
	{
		return( 0 );
	}

	d >>= sh;
	static const uint32_t btthr[ 4 ] = { 0, LZAV_OFS_TH1 >> 3,
		LZAV_OFS_TH2 >> 3, LZAV_WIN_LEN >> 3 };
	static const int ocsh[ 8 ] = { 0, 0, 2, 3, 0, 3, 5, 6 };

	uint32_t bt = 1 + ( d >= LZAV_OFS_TH1 ) + ( d >= LZAV_OFS_TH2 );
	const uint32_t sz = bt + 1;
	bt += ( d < btthr[ bt ]) * 4;
	*ns = lzavhi_cs_idx[ ocsh[ bt ]];

	return( sz );
}

/* ---------------- Encoder ---------------- */

typedef struct
{
	uint32_t pos, len, dist;
} lzavhi_seq;

static int lzavhi_compress_mref( const uint8_t* const src, uint8_t* const dst,
	const int srclen, const lzavhi_params* const prm, const int mref,
	lzavhi_mf* const mf, lzavhi_node* const nodes,
	lzavhi_match* const mpool, uint32_t* const mstart,
	lzavhi_seq* const seqs, const uint32_t cap )
{
	const size_t mref1 = (size_t) mref - 1;
	const uint32_t ipe = (uint32_t) srclen - LZAV_LIT_FIN; // Match end limit.
	const uint32_t nice = (uint32_t) prm -> nice_len;
	const uint32_t B = (uint32_t) prm -> chunk;
	const uint32_t nsl = ( prm -> one_state ? 1 : LZAVHI_NS );
	const uint32_t bpen = ( prm -> blk_cost > 0 ?
		(uint32_t) prm -> blk_cost : 1 );
	const uint32_t full = ( prm -> full_len > 0 ?
		(uint32_t) prm -> full_len : 0xFFFFFFFFu );

	uint8_t* op = dst;
	*op = (uint8_t) ( LZAV_FMT_CUR << 4 | mref );
	op++;

	if( srclen < 32 )
	{
		return( lzav_write_short_4( op, src, srclen ));
	}

	// Reset the match finder.

	memset( mf -> head, 0xFF, sizeof( uint32_t ) << mf -> hbits );

	uint8_t* cbp = op - 1;
	int csh = 0;
	uint32_t ipa = 0; // Literal anchor.

	uint32_t mfnext = 0; // Next position to insert into the tree.
	uint32_t cstart = 1; // Chunk start position.
	int s0 = 0; // Carry state at chunk start.
	uint32_t lc0 = 1; // Pending literals at chunk start (byte 0).

	lzavhi_bt( mf, 0, ipe - 0 < nice ? ipe : nice, (uint32_t) mref, NULL );
	mfnext = 1;

	while( cstart < ipe )
	{
		const uint32_t ncap = cap; // Node array positions available.
		uint32_t i, s;

		for( s = 0; s < nsl; s++ )
		{
			nodes[ s ].cost = LZAVHI_INF;
		}

		const uint32_t sl0 = ( nsl == 1 ? 0 : (uint32_t) s0 );
		nodes[ sl0 ].cost = 0;
		nodes[ sl0 ].lc = lc0;
		nodes[ sl0 ].len = 0;
		nodes[ sl0 ].from = LZAVHI_NIL;
		nodes[ sl0 ].st = (uint8_t) s0;

		uint32_t reach = 0; // Furthest node cleared.
		uint32_t far = 0; // Furthest node reached by a match.
		uint32_t mcount = 0;
		uint32_t endi = 0;
		int forced = 0;
		lzavhi_match fm = { 0, 0 };

		for( i = 0; ; i++ )
		{
			const uint32_t p = cstart + i;

			if( p >= ipe || ( i >= B && far <= i ) ||
				i + nice + 1 >= ncap )
			{
				endi = i;
				break;
			}

			// Clear nodes ahead as they are first reached.

			while( reach < i + 1 + nice && reach + 1 < ncap )
			{
				reach++;

				for( s = 0; s < nsl; s++ )
				{
					nodes[ reach * LZAVHI_NS + s ].cost = LZAVHI_INF;
				}
			}

			// Find matches at p.

			uint32_t lim = ipe - p;

			if( lim > nice )
			{
				lim = nice;
			}

			uint32_t nm = 0;
			lzavhi_match* const mm = mpool + mcount;

			if( p >= mfnext )
			{
				if( lim >= (uint32_t) mref )
				{
					nm = lzavhi_bt( mf, p, lim, (uint32_t) mref, mm );
				}
				else
				{
					lzavhi_bt( mf, p, lim < 4 ? 4 : lim, 4, NULL );
				}

				mfnext = p + 1;
			}

			mstart[ i ] = mcount;

			lzavhi_node* const nd = nodes + (size_t) i * LZAVHI_NS;
			lzavhi_node* const nn = nd + LZAVHI_NS;

			// Literal transitions.

			for( s = 0; s < nsl; s++ )
			{
				if( nd[ s ].cost == LZAVHI_INF )
				{
					continue;
				}

				const uint32_t lc = nd[ s ].lc;
				const uint32_t c = nd[ s ].cost + 64 * ( 1 +
					lzavhi_lit_hdr( lc + 1 ) - lzavhi_lit_hdr( lc ));

				if( c < nn[ s ].cost )
				{
					nn[ s ].cost = c;
					nn[ s ].lc = lc + 1;
					nn[ s ].from = i;
					nn[ s ].froms = (uint8_t) s;
					nn[ s ].st = nd[ s ].st;
					nn[ s ].len = 0;
				}
			}

			if( nm == 0 )
			{
				continue;
			}

			mcount += nm;

			if( mm[ nm - 1 ].len >= nice && nm > 0 )
			{
				// Forced long match: extend it to its true length.

				fm = mm[ nm - 1 ];
				const uint8_t* const a = src + p;
				fm.len = lzavhi_cmp( a, a - fm.dist, fm.len, ipe - p );
				endi = i;
				forced = 1;
				break;
			}

			// Match transitions.

			if( i + mm[ nm - 1 ].len > far )
			{
				far = i + mm[ nm - 1 ].len;
			}

			uint32_t k, L = (uint32_t) mref;

			for( k = 0; k < nm; k++ )
			{
				const uint32_t d = mm[ k ].dist;
				const uint32_t mlen = mm[ k ].len;
				uint32_t rcost[ LZAVHI_NS ];
				int rns[ LZAVHI_NS ];
				uint32_t live[ LZAVHI_NS ];
				uint32_t nl = 0;

				for( s = 0; s < nsl; s++ )
				{
					if( nd[ s ].cost != LZAVHI_INF )
					{
						rcost[ s ] = lzavhi_ref_cost( d, nd[ s ].lc != 0,
							nd[ s ].st, &rns[ s ]);

						if( rcost[ s ] != 0 )
						{
							rcost[ s ] = nd[ s ].cost + 64 * rcost[ s ] + bpen;
							live[ nl++ ] = s;
						}
					}
				}

				for( ; L <= mlen; L++ )
				{
					if( L > full && L < mlen )
					{
						L = mlen;
					}

					lzavhi_node* const dn = nd + (size_t) L * LZAVHI_NS;
					const uint32_t ext = 64 *
						lzavhi_len_ext( L - (uint32_t) mref1 );

					uint32_t j;

					for( j = 0; j < nl; j++ )
					{
						s = live[ j ];
						const uint32_t c = rcost[ s ] + ext;
						lzavhi_node* const t = dn + ( nsl == 1 ? 0 : rns[ s ]);

						if( c < t -> cost )
						{
							t -> cost = c;
							t -> lc = 0;
							t -> from = i;
							t -> froms = (uint8_t) s;
							t -> st = (uint8_t) rns[ s ];
							t -> len = L;
							t -> dist = d;
						}
					}
				}
			}
		}

		// Choose the best state at the end node.

		uint32_t bs = 0, bc = LZAVHI_INF;

		for( s = 0; s < nsl; s++ )
		{
			const lzavhi_node* const e = nodes + (size_t) endi * LZAVHI_NS + s;

			if( e -> cost == LZAVHI_INF )
			{
				continue;
			}

			uint32_t c = e -> cost;

			if( forced )
			{
				int ns;
				const uint32_t rc = lzavhi_ref_cost( fm.dist, e -> lc != 0,
					e -> st, &ns );

				if( rc == 0 )
				{
					continue;
				}

				c += 64 * rc + bpen;
			}

			if( c < bc )
			{
				bc = c;
				bs = s;
			}
		}

		if( bc == LZAVHI_INF )
		{
			// Forced match not encodable from any state; fall back to a
			// literal-only end (very rare).

			forced = 0;

			for( s = 0; s < nsl; s++ )
			{
				const lzavhi_node* const e =
					nodes + (size_t) endi * LZAVHI_NS + s;

				if( e -> cost < bc )
				{
					bc = e -> cost;
					bs = s;
				}
			}
		}

		// Backtrack and collect the match sequence.

		uint32_t ns = 0;
		uint32_t ci = endi, cs = bs;
		const uint32_t endlc = nodes[ (size_t) endi * LZAVHI_NS + bs ].lc;

		while( 1 )
		{
			const lzavhi_node* const e = nodes + (size_t) ci * LZAVHI_NS + cs;

			if( e -> from == LZAVHI_NIL )
			{
				break;
			}

			if( e -> len != 0 )
			{
				seqs[ ns ].pos = cstart + e -> from;
				seqs[ ns ].len = e -> len;
				seqs[ ns ].dist = e -> dist;
				ns++;
			}

			ci = e -> from;
			cs = e -> froms;
		}

		// Emit in forward order.

		while( ns != 0 )
		{
			ns--;
			const lzavhi_seq* const q = seqs + ns;
			op = lzav_write_blk_4( op, q -> pos - ipa, q -> len, q -> dist,
				src + ipa, &cbp, &csh, mref1 );

			ipa = q -> pos + q -> len;
		}

		const uint32_t pend = cstart + endi;

		if( forced )
		{
			op = lzav_write_blk_4( op, pend - ipa, fm.len, fm.dist,
				src + ipa, &cbp, &csh, mref1 );

			ipa = pend + fm.len;

			// Insert the skipped positions into the tree.

			uint32_t q;

			for( q = mfnext; q < ipa && q < ipe; q++ )
			{
				uint32_t lim = ipe - q;

				if( lim > nice )
				{
					lim = nice;
				}

				lzavhi_bt( mf, q, lim < 4 ? 4 : lim, 4, NULL );
			}

			if( mfnext < ipa )
			{
				mfnext = ipa;
			}

			cstart = ipa;
			lc0 = 0;
		}
		else
		{
			cstart = pend;
			lc0 = endlc;

			if( lc0 != pend - ipa )
			{
				return( -1 ); // Internal consistency error.
			}
		}

		s0 = lzavhi_cs_idx[ csh ];
	}

	op = lzav_write_fin_4( op, (size_t) ( srclen - (int) ipa ), src + ipa );

	return( (int) ( op - dst ));
}

/**
 * lzav-hi compression. Output is standard LZAV format-4 data, decodable by
 * the stock lzav_decompress(). `dstlen` should be at least
 * lzav_compress_bound_hi( srclen ) + 64 (mref 4 uses the same bound as mref 5
 * since an all-literal stream is never exceeded; the margin covers headers).
 *
 * @return Compressed length, or 0 on error.
 */

static int lzav_compress_hi2( const void* const src, void* const dst,
	const int srclen, const int dstlen, const lzavhi_params* prm )
{
	if( prm == NULL )
	{
		prm = &lzavhi_default_params;
	}

	if( srclen <= 0 || src == NULL || dst == NULL ||
		dstlen < lzav_compress_bound_hi( srclen ))
	{
		return( 0 );
	}

	if( srclen < 64 )
	{
		return( lzav_compress_hi( src, dst, srclen, dstlen ));
	}

	lzavhi_mf mf;
	mf.src = (const uint8_t*) src;
	mf.n = (uint32_t) srclen;
	mf.depth = (uint32_t) prm -> depth;
	mf.hbits = 16;

	while( mf.hbits < 24 && ( 1u << mf.hbits ) < (uint32_t) srclen )
	{
		mf.hbits++;
	}

	const uint32_t cap = (uint32_t) prm -> chunk * 2 +
		(uint32_t) prm -> nice_len + 8;

	mf.head = (uint32_t*) malloc( sizeof( uint32_t ) << mf.hbits );
	mf.son = (uint32_t*) malloc( sizeof( uint32_t ) * 2 * (size_t) srclen );

	lzavhi_node* const nodes = (lzavhi_node*) malloc(
		sizeof( lzavhi_node ) * LZAVHI_NS * ( (size_t) cap + 1 ));

	// Each position yields at most `depth` matches (one per tree node
	// visited), so the pool cannot overflow within a chunk.

	const size_t mpsize = (size_t) cap * ( (size_t) prm -> depth + 1 ) + 64;
	lzavhi_match* const mpool = (lzavhi_match*) malloc(
		sizeof( lzavhi_match ) * mpsize );

	uint32_t* const mstart = (uint32_t*) malloc( sizeof( uint32_t ) * cap );
	lzavhi_seq* const seqs = (lzavhi_seq*) malloc(
		sizeof( lzavhi_seq ) * ( cap + 1 ));

	uint8_t* tmp = NULL;
	int res = 0;

	if( mf.head == NULL || mf.son == NULL || nodes == NULL ||
		mpool == NULL || mstart == NULL || seqs == NULL )
	{
		goto done;
	}

	if( prm -> mref == 4 || prm -> mref == 5 )
	{
		res = lzavhi_compress_mref( (const uint8_t*) src, (uint8_t*) dst,
			srclen, prm, prm -> mref, &mf, nodes, mpool, mstart, seqs, cap );
	}
	else
	{
		tmp = (uint8_t*) malloc( (size_t) dstlen );

		if( tmp == NULL )
		{
			goto done;
		}

		const int r5 = lzavhi_compress_mref( (const uint8_t*) src,
			(uint8_t*) dst, srclen, prm, 5, &mf, nodes, mpool, mstart, seqs,
			cap );

		const int r4 = lzavhi_compress_mref( (const uint8_t*) src, tmp,
			srclen, prm, 4, &mf, nodes, mpool, mstart, seqs, cap );

		res = r5;

		if( r4 > 0 && ( r5 <= 0 || r4 < r5 ))
		{
			memcpy( dst, tmp, (size_t) r4 );
			res = r4;
		}
	}

	if( res < 0 )
	{
		res = 0;
	}

done:
	free( tmp );
	free( seqs );
	free( mstart );
	free( mpool );
	free( nodes );
	free( mf.son );
	free( mf.head );

	return( res );
}

#endif // LZAV_HI_INCLUDED
