
#ifndef SGS_IDBG_H_INCLUDED
#define SGS_IDBG_H_INCLUDED

#include <sgscript.h>
#include <sgs_util.h>

typedef
struct _sgs_IDbg
{
	SGS_CTX;
	sgs_PrintFunc pfn;
	void* pctx;
	sgs_MemBuf input;
	char iword[ 32 ];
	int inside;
	int stkoff;
	int stksize;
}
sgs_IDbg;

#define SGS_IDBG sgs_IDbg* D

int sgs_InitIDbg( SGS_CTX, SGS_IDBG );
int sgs_CloseIDbg( SGS_CTX, SGS_IDBG );


#endif
