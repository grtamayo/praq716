/*
	Filename:     praq7172.c
	Description:  PPP style or simply LZP. Outputs (1 bit + LZP matched length) or (1 bit + mismatched length + mismatched bytes).
	Written by:   Gerald R. Tamayo, 7/25/2023, 7/5/2024
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>   /* C99 */
#include <time.h>
#include "gtbitio3.c"
#include "ucodes3.c"

#define EOF_CODE    253

/* PPP_BLOCKBITS must be >= 3 (multiple of 8 bytes blocksize) */
#define PPP_BLOCKBITS  20
#define PPP_BLOCKSIZE  (1<<PPP_BLOCKBITS)

enum {
	/* modes */
	COMPRESS,
	DECOMPRESS,
};

typedef struct {
	char alg[8];
	int ppp_WBITS;
} file_stamp;

unsigned char *win_buf;   /* the prediction buffer or "GuessTable". */
unsigned char pattern[ PPP_BLOCKSIZE ];   /* the "look-ahead" buffer. */
int ppp_WBITS, ppp_WSIZE, ppp_WMASK;

void copyright( void );
void   compress_LZP( unsigned char w[], unsigned char p[] );
void decompress_LZP( unsigned char w[] );

void usage( void )
{
	fprintf(stderr, "\n Usage: praq7172 c[N]|d infile outfile\n"
		"\n Commands:\n  c[N] = encoding/compression (where N is the bitsize of prediction table, N=16..30).\n  d = decoding.\n"
	);
	copyright();
	exit(0);
}

int main( int argc, char *argv[] )
{
	float ratio = 0.0;
	int mode = -1;
	file_stamp fstamp;
	
	clock_t start_time = clock();
	
	if ( argc < 3 || argc > 4 ) usage();
	init_buffer_sizes( (1<<20) );
	
	/* Process options, get ppp_WBITS. */
	if ( tolower(argv[1][0]) == 'c' ) {
		mode = COMPRESS;
		if ( argv[1][1] == '\0' ) ppp_WBITS = 20;  /* default 1MB table size */
		else ppp_WBITS = atoi(&argv[1][1]);
		if ( argv[1][1] == '0' || ppp_WBITS == 0 ) usage();
		if ( ppp_WBITS < 16 ) ppp_WBITS = 16;
		else if ( ppp_WBITS > 30 ) ppp_WBITS = 30;
	}
	else if ( tolower(argv[1][0]) == 'd' ) {
		mode = DECOMPRESS;
		if ( argv[1][1] != '\0' ) usage();
	}
	else usage();
	
	if ( (gIN=fopen( argv[2], "rb" )) == NULL ) {
		fprintf(stderr, "\nError opening input file.");
		return 0;
	}
	if ( (pOUT=fopen( argv[3], "wb" )) == NULL ) {
		fprintf(stderr, "\nError opening output file.");
		return 0;
	}
	init_put_buffer();
	
	if ( mode == COMPRESS ){
		/* Write the FILE STAMP. */
		strcpy( fstamp.alg, "PRAQ717" );
		fwrite( &fstamp, sizeof(file_stamp), 1, pOUT );
		nbytes_out = sizeof(file_stamp);
	}
	else if ( mode == DECOMPRESS ){
		/* Read the file stamp. */
		fread( &fstamp, sizeof(file_stamp), 1, gIN );
		ppp_WBITS = fstamp.ppp_WBITS;
	}
	ppp_WSIZE = 1 << ppp_WBITS;
	ppp_WMASK = (ppp_WSIZE-1);
	
	/* allocate memory for win_buf. */
	win_buf = (unsigned char *) malloc( sizeof(unsigned char) * ppp_WSIZE );
	if ( !win_buf ) {
		fprintf(stderr, "\n Error alloc: Prediction Table (win_buf).");
		goto halt_prog;
	}
	/* initialize prediction buffer to all zero (0) values. */
	memset( win_buf, 0, ppp_WSIZE );
	
	/* finally, compress or decompress */
	if ( mode == COMPRESS ){
		fprintf(stderr, "\n Prediction Table size used (%d bits)  = %u bytes", ppp_WBITS, (unsigned int) ppp_WSIZE );
		fprintf(stderr, "\n\n Encoding [ %s to %s ] ...", argv[2], argv[3] );
		compress_LZP( win_buf, pattern );
	}
	else if ( mode == DECOMPRESS ){
		init_get_buffer();
		nbytes_read = sizeof(file_stamp);
		fprintf(stderr, "\n Decoding...");
		decompress_LZP( win_buf );
		nbytes_read = get_nbytes_read();
		free_get_buffer();
	}
	flush_put_buffer();
	
	if ( mode == COMPRESS ) {
		rewind( pOUT );
		fstamp.ppp_WBITS = ppp_WBITS;
		fwrite( &fstamp, sizeof(file_stamp), 1, pOUT );
	}
	
	fprintf(stderr, "done.\n  %s (%lld) -> %s (%lld)", 
		argv[2], nbytes_read, argv[3], nbytes_out);
	if ( mode == COMPRESS ) {
		ratio = (((float) nbytes_read - (float) nbytes_out) /
			(float) nbytes_read ) * (float) 100;
		fprintf(stderr, "\n Compression ratio: %3.2f %%", ratio );
	}
	fprintf(stderr, " in %3.2f secs.\n", (double) (clock()-start_time) / CLOCKS_PER_SEC );
	
	halt_prog:
	
	free_put_buffer();
	if ( win_buf ) free( win_buf );
	fclose( gIN );
	fclose( pOUT );
	return 0;
}

void copyright( void )
{
	fprintf(stderr, "\n Written by: Gerald R. Tamayo (c) 2022-2024\n");
}

void compress_LZP( unsigned char w[], unsigned char p[] )
{
	int i, blen=0, clen=0, n, nread, prev=0;  /* prev = context hash */
	unsigned char c, cbuf[PPP_BLOCKSIZE];
	
	while ( (nread=fread(p, 1, PPP_BLOCKSIZE, gIN)) ){
		n = 0;
		while ( n < nread ) {
			if ( w[prev] == (c=p[n]) ){
				if ( clen ){
					put_ZERO();
					put_unary(clen-1);
					for ( i = 0; i < clen; i++ ){
						put_nbits(cbuf[i], 8);
						if ( cbuf[i] == EOF_CODE ) put_ZERO();
					}
					clen = 0;
				}
				blen++;
			}
			else {
				if ( blen ) {
					put_ONE();
					put_unary(--blen);
					blen = 0;
				}
				cbuf[clen++] = c;
				w[prev] = c;
			}
			prev = ((prev<<5)+c) & ppp_WMASK;
			n++;
		}
		nbytes_read += nread;
	}
	/* flag EOF */
	if ( blen ) {
		put_ONE();
		put_unary(--blen);
	}
	if ( clen ){
		put_ZERO();
		put_unary(clen-1);
		for ( i = 0; i < clen; i++ ){
			put_nbits(cbuf[i], 8);
			if ( cbuf[i] == EOF_CODE ) put_ZERO();
		}
	}
	put_ZERO();
	put_unary(0);
	put_nbits(EOF_CODE, 8);
	put_ONE();
}

void decompress_LZP( unsigned char w[] )
{
	int i, blen, c, clen, prev = 0;  /* prev = context hash */
	
	do {
		if ( get_bit() ){
			blen = get_unary()+1;
			do {
				pfputc( c=w[prev] );
				prev = ((prev<<5)+c) & ppp_WMASK;
			} while ( --blen );
		}
		else {
			clen = get_unary()+1;
			for ( i = 0; i < clen; i++ ){
				if ( (c=get_nbits(8)) == EOF_CODE && get_bit() ) return;
				pfputc( c );
				w[prev] = c;
				prev = ((prev<<5)+c) & ppp_WMASK;
			}
		}
	} while ( 1 );
}
