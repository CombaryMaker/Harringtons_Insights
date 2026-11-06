/*
 * bench.c - in-memory benchmark: stock LZAV default, stock LZAV HI,
 * lzav-hi (this project), LZ4 -1, LZ4HC -9, LZ4HC -12.
 *
 * Every lzav-hi output is decoded with the STOCK lzav_decompress() from the
 * unmodified lzav.h and compared byte for byte with the input.
 *
 * Usage: bench [-r runs] [-c codec,codec,...] file...
 * Codecs: lzav, lzavhi, hi2fast, hi2, hi2m5, lz4, lz4hc9, lz4hc12
 *
 * MIT License, Copyright (c) 2026 Michael Hugh Harrington.
 */

#include "lzav.h"
#include "lzav_hi.h"
#include "lz4.h"
#include "lz4hc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return( ts.tv_sec + ts.tv_nsec * 1e-9 );
}

static int cmpd( const void* a, const void* b )
{
	const double x = *(const double*) a, y = *(const double*) b;
	return( x < y ? -1 : x > y );
}

static double median( double* v, int n )
{
	qsort( v, n, sizeof( double ), cmpd );
	return( n & 1 ? v[ n / 2 ] : 0.5 * ( v[ n / 2 - 1 ] + v[ n / 2 ]));
}


static int do_comp( const char* c, const void* s, void* d, int sl, int dl )
{
	if( !strcmp( c, "lzav" )) return( lzav_compress_default( s, d, sl, dl ));
	if( !strcmp( c, "lzavhi" )) return( lzav_compress_hi( s, d, sl, dl ));
	if( !strcmp( c, "hi2" )) return( lzav_compress_hi2( s, d, sl, dl,
		&lzavhi_default_params ));
	if( !strcmp( c, "hi2fast" )) return( lzav_compress_hi2( s, d, sl, dl,
		&lzavhi_fast_params ));
	if( !strcmp( c, "hi2m5" )) return( lzav_compress_hi2( s, d, sl, dl,
		&lzavhi_m5_params ));
	if( !strcmp( c, "lz4" )) return( LZ4_compress_default( s, d, sl, dl ));
	if( !strcmp( c, "lz4hc9" )) return( LZ4_compress_HC( s, d, sl, dl, 9 ));
	if( !strcmp( c, "lz4hc12" )) return( LZ4_compress_HC( s, d, sl, dl, 12 ));
	fprintf( stderr, "unknown codec %s\n", c );
	exit( 2 );
}

static int do_dec( const char* c, const void* s, void* d, int sl, int ol )
{
	if( !strncmp( c, "lz4", 3 )) return( LZ4_decompress_safe( s, d, sl, ol ));
	return( lzav_decompress( s, d, sl, ol )); // STOCK decoder for all LZAV.
}

int main( int argc, char** argv )
{
	int runs = 5, ai = 1;
	char codecs[ 512 ] = "lzav,lzavhi,hi2fast,hi2,hi2m5,lz4,lz4hc9,lz4hc12";

	while( ai < argc && argv[ ai ][ 0 ] == '-' )
	{
		if( !strcmp( argv[ ai ], "-r" )) runs = atoi( argv[ ++ai ]);
		else if( !strcmp( argv[ ai ], "-c" ))
		{
			strncpy( codecs, argv[ ++ai ], sizeof( codecs ) - 1 );
		}
		ai++;
	}

	char* clist[ 32 ];
	int nc = 0;
	char* tok = strtok( codecs, "," );

	while( tok && nc < 32 )
	{
		clist[ nc++ ] = tok;
		tok = strtok( NULL, "," );
	}

	double tot_in[ 32 ] = { 0 }, tot_out[ 32 ] = { 0 }, tot_ct[ 32 ] = { 0 },
		tot_dt[ 32 ] = { 0 };

	printf( "%-12s %-8s %12s %12s %8s %10s %9s %10s %9s %s\n", "file", "codec",
		"orig", "comp", "ratio", "c_time_s", "c_MB/s", "d_time_s", "d_MB/s",
		"verify" );

	for( ; ai < argc; ai++ )
	{
		FILE* f = fopen( argv[ ai ], "rb" );

		if( !f ) { perror( argv[ ai ]); return( 1 ); }

		fseek( f, 0, SEEK_END );
		const long n = ftell( f );
		fseek( f, 0, SEEK_SET );
		uint8_t* in = malloc( n + 64 );

		if( fread( in, 1, n, f ) != (size_t) n ) { return( 1 ); }

		fclose( f );

		const int bound = lzav_compress_bound_hi( n ) +
			LZ4_compressBound( n ) + 1024;

		uint8_t* cb = malloc( bound );
		uint8_t* db = malloc( n + 64 );
		const char* bn = strrchr( argv[ ai ], '/' );
		bn = bn ? bn + 1 : argv[ ai ];

		for( int c = 0; c < nc; c++ )
		{
			double ct[ 64 ], dt[ 64 ];
			int cl = 0;

			for( int r = 0; r < runs; r++ )
			{
				const double t0 = now();
				cl = do_comp( clist[ c ], in, cb, (int) n, bound );
				ct[ r ] = now() - t0;
			}

			int ok = 1;

			for( int r = 0; r < runs; r++ )
			{
				memset( db, 0, n );
				const double t0 = now();
				const int dl = do_dec( clist[ c ], cb, db, cl, (int) n );
				dt[ r ] = now() - t0;

				if( dl != n || memcmp( db, in, n ) != 0 ) ok = 0;
			}

			const double mc = median( ct, runs ), md = median( dt, runs );

			printf( "%-12s %-8s %12ld %12d %7.3fx %10.4f %9.2f %10.4f %9.1f %s\n",
				bn, clist[ c ], n, cl, (double) n / cl, mc, n / mc / 1e6, md,
				n / md / 1e6, ok ? "OK" : "FAIL" );

			fflush( stdout );

			tot_in[ c ] += n;
			tot_out[ c ] += cl;
			tot_ct[ c ] += mc;
			tot_dt[ c ] += md;

			if( !ok ) return( 3 );
		}

		free( in ); free( cb ); free( db );
	}

	printf( "\nTOTAL (sum of per-file medians)\n" );

	for( int c = 0; c < nc; c++ )
	{
		printf( "%-12s %-8s %12.0f %12.0f %7.3fx %10.4f %9.2f %10.4f %9.1f "
			"(%.2f%% of original)\n", "TOTAL", clist[ c ], tot_in[ c ],
			tot_out[ c ], tot_in[ c ] / tot_out[ c ], tot_ct[ c ],
			tot_in[ c ] / tot_ct[ c ] / 1e6, tot_dt[ c ],
			tot_in[ c ] / tot_dt[ c ] / 1e6,
			100.0 * tot_out[ c ] / tot_in[ c ]);
	}

	return( 0 );
}
