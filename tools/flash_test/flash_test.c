#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "../hidapi.h"
#include "../hidapi.c"

#define VID 0x303a
#define PID 0x4004

hid_device * hd;
#ifdef WIN32
const int chunksize = 244;
const int const_send = 255;
#else
const int chunksize = 128;
const int const_send = 144;
#endif
int tries = 0;
int alignlen = 4;

uint8_t rdata[512]; //Must be plenty big.
int sector_size = 4096;

int StringToNumberHexOrInt( const char * str )
{
	if( !str ) return 0;
	if( strlen( str ) > 1 )
	{
		if( str[0] == '0' && str[1] == 'x' )
		{
			return strtol( str+2, 0, 16 );
		}
	}
	return atoi( str );
}

int main( int argc, char ** argv )
{
	if( argc != 4 && argc != 5 )
	{
		goto help;
	}
	
	uint32_t flash_start = StringToNumberHexOrInt( argv[3] );
		
	hid_init();
	hd = hid_open( VID, PID, 0);
	if( !hd ) { fprintf( stderr, "Could not open USB\n" ); return -94; }

	struct timeval tv_start, tv_end;
	gettimeofday( &tv_start, 0 );

	if( strcmp( argv[1], "write" ) == 0 )
	{
		if( argc != 4 ) goto help;
		struct stat sbuf = { 0 };
		int r = stat( argv[2], &sbuf);
		int bloblen = sbuf.st_size;
		if( r < 0 )
		{
			fprintf( stderr, "Could not open file\n" );
			return -1;
		}
		if( bloblen == 0 )
		{
			fprintf( stderr, "No file to write\n" );
			return -1;
		}
		
		printf( "Writing file %s to 0x%x (length: %d)\n", argv[2], flash_start, bloblen );
		int blobkclen = ( bloblen + alignlen - 1 ) / alignlen * alignlen;
		uint8_t * blob;
		{
			FILE * binary_blob = fopen( argv[2], "rb" );
			if( !binary_blob )
			{
				fprintf( stderr, "ERROR: Could not open %s.\n", argv[2] );
				return -96;
			}
			blob = calloc( blobkclen, 1 );
			if( fread( blob, bloblen, 1, binary_blob ) != 1 )
			{
				fprintf( stderr, "ERROR: Could not read %s.\n", argv[2] );
				return -97;
			}
			fclose( binary_blob );
		}

		// Now, we have blob and bloblen.

		printf( "Erasing\n" );
		if( flash_start & (sector_size-1) )
		{
			fprintf( stderr, "WARNING: You are doing a non-block-aligned-write.  We will NOT perform an erase.\n" );
		}
		else
		{
			// Erase.
			int eraselen = ( blobkclen + sector_size - 1 ) / sector_size * sector_size;
			rdata[0] = 170;
			rdata[1] = 0x10;
			rdata[2] = flash_start & 0xff;
			rdata[3] = flash_start >> 8;
			rdata[4] = flash_start >> 16;
			rdata[5] = flash_start >> 24;
			rdata[6] = eraselen & 0xff;
			rdata[7] = eraselen >> 8;
			rdata[8] = eraselen >> 16;
			rdata[9] = eraselen >> 24;
			do
			{
				r = hid_send_feature_report( hd, rdata, 65 );
				if( tries++ > 10 ) { fprintf( stderr, "Error sending feature report on command %d (%d)\n", rdata[1], r ); return -85; }
			} while ( r < 10 );
			tries = 0;
		}
		
		printf( "Writing\n" );
		int offset;
		int lastpercent = 0;
		for( offset = 0; offset < blobkclen; offset+= chunksize )
		{
			int percent = ((uint64_t)(offset)) * 1000LL / blobkclen;
			if( percent >= lastpercent + 100 )
			{
				printf( "%d%%\n", percent/10 );
				fflush( stdout );
				lastpercent += 100;
			}
			uint32_t wp = flash_start + offset;
			// Write
			rdata[0] = 171;
			rdata[1] = 0x11;
			rdata[2] = wp & 0xff;
			rdata[3] = wp >> 8;
			rdata[4] = wp >> 16;
			rdata[5] = wp >> 24;
			int wl = blobkclen - offset;
			if( wl > chunksize ) wl = chunksize;
			rdata[6] = wl & 0xff;
			rdata[7] = wl >> 8;
			memcpy( rdata + 8, blob + offset, wl );
			do
			{
				r = hid_send_feature_report( hd, rdata, const_send );
				if( tries++ > 10 ) { fprintf( stderr, "Error sending feature report on command %d (%d)\n", rdata[1], r ); return -85; }
			} while ( r < wl+8  );
			tries = 0;
		}
		gettimeofday( &tv_end, 0 );
		int elapsed_us = (tv_end.tv_sec - tv_start.tv_sec)*1000000 + (tv_end.tv_usec - tv_start.tv_usec);
		printf( "Done. Took %3.3f seconds. %.2f kbit/s (Erase time included)\n", elapsed_us / 1000000.0f, bloblen*8000.0f/elapsed_us );
	}
	else if( strcmp( argv[1], "read" ) == 0 )
	{
		if( argc != 5 ) goto help;
		int r;
		uint32_t length = StringToNumberHexOrInt( argv[4] );
		FILE * f = fopen( argv[2], "wb" );
		if( !f )
		{
			fprintf( stderr, "Error: couldn't open file for writing\n" );
			return -5;
		}
		int offset;
		
		int blobkclen = ( length + alignlen - 1 ) / alignlen * alignlen;
		printf( "Reading %d bytes from 0x%x\n", length, flash_start );
		int lastpercent = 0;
		for( offset = 0; offset < blobkclen; offset+= chunksize )
		{
			int percent = ((uint64_t)(offset)) * 1000LL / blobkclen;
			if( percent >= lastpercent + 100 )
			{
				printf( "%d%%\n", percent/10 );
				fflush( stdout );
				lastpercent += 100;
			}
			
			uint32_t wp = flash_start + offset;
			// Write
			rdata[0] = 170;
			rdata[1] = 0x12;
			rdata[2] = wp & 0xff;
			rdata[3] = wp >> 8;
			rdata[4] = wp >> 16;
			rdata[5] = wp >> 24;
			
			int wl = length - offset;
			if( wl > chunksize ) wl = chunksize;

			rdata[6] = wl & 0xff;
			rdata[7] = wl >> 8;
			do
			{
				r = hid_send_feature_report( hd, rdata, 65 );
				if( tries++ > 10 ) { fprintf( stderr, "Error sending feature report on command %d (%d)\n", rdata[1], r ); return -85; }
			} while ( r < 8 );
			tries = 0;

			do
			{
				memset( rdata, 0, sizeof( rdata ) );
				rdata[0] = 170;
				r = hid_get_feature_report( hd, rdata, sizeof(rdata) );
				//printf( "%d %d [%x %x %x %x %x %x]\n", wl, r, rdata[0], rdata[1], rdata[2], rdata[3], rdata[4], rdata[5] );
				if( tries++ > 10 ) { fprintf( stderr, "Error getting feature report on command %d (%d)\n", rdata[1], r ); return -85; }
			} while ( r < wl );
			tries = 0;
		
			int chars = wl;
			fwrite( rdata, wl, 1, f );
			
		}
		fclose( f );
		gettimeofday( &tv_end, 0 );
		int elapsed_us = (tv_end.tv_sec - tv_start.tv_sec)*1000000 + (tv_end.tv_usec - tv_start.tv_usec);
		printf( "Done. Took %3.3f seconds. %.2f kbit/s\n", elapsed_us / 1000000.0f, length*8000.0f/elapsed_us );
	}
	else
	{
		goto help;
	}

	hid_close( hd );

	return 0;	
help:
	fprintf( stderr, "Error: Usage: flash_test [read/write] [file to flash] [where to write (0xHEX or 1234 dec)] {size, read only}\n" );
	return -1;
}

