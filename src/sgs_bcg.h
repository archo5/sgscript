
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
	MemBuf	consts;
	MemBuf	code;
}
sgs_CompFunc;


/* - bytecode generator */
sgs_CompFunc* sgsBC_Generate( SGS_CTX, FTNode* tree );
void sgsBC_Dump( sgs_CompFunc* func );
void sgsBC_Free( SGS_CTX, sgs_CompFunc* func );


#endif /* SGS_UNIT_H_INCLUDED */
