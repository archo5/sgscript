
#ifndef SGS_IDBG_H_INCLUDED
#define SGS_IDBG_H_INCLUDED

#include <sgscript.h>
#include <sgs_util.h>

typedef
struct _sgs_IDbg
{
	SGS_CTX;
	sgs_MsgFunc pfn;
	void* pctx;
	sgs_MemBuf input;
	char iword[ 32 ];
	int inside;
	ptrdiff_t stkoff;
	ptrdiff_t stksize;

	int minlev;
}
sgs_IDbg;

#define SGS_IDBG sgs_IDbg* D

int sgs_InitIDbg( SGS_CTX, SGS_IDBG );
int sgs_CloseIDbg( SGS_CTX, SGS_IDBG );


#endif
