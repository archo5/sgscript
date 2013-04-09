
#ifndef SGS_UNIT_H_INCLUDED
#define SGS_UNIT_H_INCLUDED

#include "sgscript.h"
#include "sgs_proc.h"

/*
	Intermediate function
	- keeps the function data between compiler stages
*/

typedef
struct _sgs_IntFunc
{
	MemBuf consts;
	MemBuf code;
	MemBuf lnbuf;
	int	   gotthis;
	int	   numargs;
}
sgs_CompFunc;


/* - bytecode generator */
sgs_CompFunc* sgsBC_Generate( SGS_CTX, FTNode* tree );
void sgsBC_Dump( sgs_CompFunc* func );
void sgsBC_Free( SGS_CTX, sgs_CompFunc* func );


/*
	Serialized bytecode
*/
int sgsBC_Func2Buf( SGS_CTX, sgs_CompFunc* func, MemBuf* outbuf );

/* assumes headers have already been validated (except size) but are still in the buffer */
int sgsBC_Buf2Func( SGS_CTX, const char* fn, char* buf, int32_t size, sgs_CompFunc** outfunc );

/* validates header size and bytes one by one (except last flag byte)
-- will return header_size on success and failed byte position on failure */
int sgsBC_ValidateHeader( char* buf, int32_t size );
#define SGS_HEADER_SIZE 14
#define SGS_MIN_BC_SIZE 14 + SGS_HEADER_SIZE
#define SGSBC_FLAG_LITTLE_ENDIAN 0x01


#endif /* SGS_UNIT_H_INCLUDED */
