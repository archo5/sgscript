
#ifndef SGS_CTX_H_INCLUDED
#define SGS_CTX_H_INCLUDED

#include "sgs_util.h"
#include "sgs_proc.h"

/*
	Context handling
*/

typedef struct _sgs_BreakInfo sgs_BreakInfo;
struct _sgs_BreakInfo
{
	sgs_BreakInfo* next;
	uint32_t jdoff;  /* jump data offset */
	uint16_t numlp;  /* which loop */
	uint8_t  iscont; /* is a "continue"? */
};

typedef
struct _sgs_FuncCtx
{
	int32_t func;
	int32_t regs, lastreg;
	StrBuf  vars;
	StrBuf  gvars;
	int32_t loops;
	sgs_BreakInfo* binfo;
}
sgs_FuncCtx;

/* compilation state */
/* - user (transfer TODO) */
#define SGS_STOP_ON_FIRST_ERROR		0x0001
#define SGS_ADD_DEBUG_INFO			0x0002
/* - auto */
#define SGS_HAS_ERRORS				0x00010000
#define SGS_MUST_STOP				(0x00020000 | SGS_HAS_ERRORS)


typedef sgs_Variable* sgs_VarPtr;

struct _sgs_Context
{
	/* info output */
	sgs_PrintFunc print_fn;  /* printing function */
	void*         print_ctx; /* printing context */

	/* compilation */
	uint32_t      state;
	sgs_FuncCtx*  fctx;      /* ByteCodeGen */

	/* virtual machine */
	sgs_VarPtr    stack_base;
	int           stack_mem;
	sgs_VarPtr    stack_off;
	sgs_VarPtr    stack_top;

	int           call_args;
	int           call_expect;
	int           call_this;
	sgs_StackFrame* sf_first;
	sgs_StackFrame* sf_last;

	HashTable     data;

	object_t*     objs;
	int32_t       objcount;
	uint8_t       redblue;
	sgs_VarPtr    gclist;
	int           gclist_size;

	/* special functions */
	sgs_CFunc     array_func;
	sgs_CFunc     dict_func;
};


#endif /* SGS_CTX_H_INCLUDED */
