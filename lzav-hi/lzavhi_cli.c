/*
 * lzavhi_cli.c - command-line front end for independent verification.
 *
 *   lzavhi c [-fast|-m5] IN OUT       compress with lzav-hi (default: max, mref 4)
 *   lzavhi s IN OUT                   compress with STOCK lzav_compress_hi (for comparison)
 *   lzavhi d IN OUT                   decompress with the STOCK lzav_decompress
 *
 * File layout: 8-byte little-endian original length, then the raw LZAV
 * stream exactly as lzav_compress_hi() would produce it (format 4). Strip
 * the first 8 bytes to feed the stream to any LZAV decoder.
 *
 * MIT License, Copyright (c) 2026 Michael Hugh Harrington.
 */

#include "lzav.h"
#include "lzav_hi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char* load( const char* p, long* n )
{
	FILE* f = fopen( p, "rb" );
	if( !f ) { perror( p ); exit( 1 ); }
	fseek( f, 0, SEEK_END ); *n = ftell( f ); fseek( f, 0, SEEK_SET );
	unsigned char* b = (unsigned char*) malloc( *n + 64 );
	if( fread( b, 1, *n, f ) != (size_t) *n ) { fprintf( stderr, "read error\n" ); exit( 1 ); }
	fclose( f );
	return( b );
}

static void save( const char* p, const void* a, size_t na, const void* b,
	size_t nb )
{
	FILE* f = fopen( p, "wb" );
	if( !f ) { perror( p ); exit( 1 ); }
	if( fwrite( a, 1, na, f ) != na || fwrite( b, 1, nb, f ) != nb )
	{
		fprintf( stderr, "write error\n" ); exit( 1 );
	}
	fclose( f );
}

int main( int argc, char** argv )
{
	lzavhi_params prm = lzavhi_default_params;
	int ai = 2;

	if( argc < 4 ) goto usage;

	if( argv[ 1 ][ 0 ] == 'c' && argv[ 2 ][ 0 ] == '-' )
	{
		if( !strcmp( argv[ 2 ], "-fast" )) prm = lzavhi_fast_params;
		else if( !strcmp( argv[ 2 ], "-m5" )) prm = lzavhi_m5_params;
		else goto usage;
		ai = 3;
	}

	if( argc < ai + 2 ) goto usage;

	long n;
	unsigned char* in = load( argv[ ai ], &n );

	if( argv[ 1 ][ 0 ] == 'c' || argv[ 1 ][ 0 ] == 's' )
	{
		if( n > 0x7FFFFF00L ) { fprintf( stderr, "input too large\n" ); return( 1 ); }
		const int cap = lzav_compress_bound_hi( (int) n ) + 64;
		unsigned char* out = (unsigned char*) malloc( cap );
		const int cl = ( argv[ 1 ][ 0 ] == 'c' ?
			lzav_compress_hi2( in, out, (int) n, cap, &prm ) :
			lzav_compress_hi( in, out, (int) n, cap ));
		if( cl <= 0 && n > 0 ) { fprintf( stderr, "compression failed\n" ); return( 1 ); }
		unsigned char hdr[ 8 ];
		for( int i = 0; i < 8; i++ ) hdr[ i ] = (unsigned char) ( (unsigned long long) n >> ( 8 * i ));
		save( argv[ ai + 1 ], hdr, 8, out, (size_t) cl );
		printf( "%ld -> %d bytes (%.3fx)\n", n, cl + 8, (double) n / ( cl + 8 ));
		return( 0 );
	}

	if( argv[ 1 ][ 0 ] == 'd' )
	{
		if( n < 8 ) { fprintf( stderr, "bad file\n" ); return( 1 ); }
		unsigned long long ol = 0;
		for( int i = 0; i < 8; i++ ) ol |= (unsigned long long) in[ i ] << ( 8 * i );
		if( ol > 0x7FFFFF00ULL ) { fprintf( stderr, "bad length\n" ); return( 1 ); }
		unsigned char* out = (unsigned char*) malloc( ol + 64 );
		const int r = lzav_decompress( in + 8, out, (int) ( n - 8 ), (int) ol );
		if( r != (int) ol ) { fprintf( stderr, "decode error %d\n", r ); return( 1 ); }
		save( argv[ ai + 1 ], out, ol, "", 0 );
		printf( "decoded %llu bytes with stock lzav_decompress\n", ol );
		return( 0 );
	}

usage:
	fprintf( stderr, "usage: lzavhi c [-fast|-m5] IN OUT | s IN OUT | d IN OUT\n" );
	return( 2 );
}
