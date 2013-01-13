
#ifndef SGS_PROC_H_INCLUDED
#define SGS_PROC_H_INCLUDED


#include "sgs_util.h"
#include "sgs_fnt.h"


#define CONSTENC( x ) ((x)|0x8000)
#define CONSTDEC( x ) ((x)&0x7fff)

typedef enum sgs_Instruction_e
{
	SI_NOP = 0,

	SI_PUSH,	/* (i16 src)			push register/constant */
	SI_PUSHN,	/* (u8 N)				push N nulls */
	SI_POPN,	/* (u8 N)				pop N items */
	SI_POPR,	/* (i16 out)			pop item to register */

	SI_RETN,	/* (u8 N)				exit current frame of execution, preserve N output arguments */
	SI_JUMP,	/* (i16 off)			add to instruction pointer */
	SI_JMPF,	/* (i16 src, off)		jump (add to instr.ptr.) if false */
	SI_CALL,	/* (u8 args, expect, i16 src)	call a variable */

	SI_GETVAR,	/* (i16 out, name)		<varname> => <value> */
	SI_SETVAR,	/* (i16 name, src)		<varname> <value> => set <value> to <varname> */
	SI_GETPROP,	/* (i16 out, var, name)	<var> <prop> => <var> */
	SI_SETPROP,	/* (i16 var, name, src)	<var> <prop> <value> => set a <prop> of <var> to <value> */
	SI_GETINDEX,/* -- || -- */
	SI_SETINDEX,/* -- || -- */

	/* operators */
	/*
		A: (i16 out, s1, s2)
		B: (i16 out, s1)
	*/
	SI_SET,		/* B */
	SI_COPY,
	SI_CONCAT,	/* A */
	SI_BOOL_AND,
	SI_BOOL_OR,
	SI_NEGATE,	/* B */
	SI_BOOL_INV,
	SI_INVERT,

	SI_INC,		/* B */
	SI_DEC,
	SI_ADD,		/* A */
	SI_SUB,
	SI_MUL,
	SI_DIV,
	SI_MOD,

	SI_AND,		/* A */
	SI_OR,
	SI_XOR,
	SI_LSH,
	SI_RSH,

	SI_SEQ,		/* A */
	SI_SNEQ,
	SI_EQ,
	SI_NEQ,
	SI_LT,
	SI_GTE,
	SI_GT,
	SI_LTE,

	/* specials */
	SI_ARRAY,	/* (u8 args, i16 out) */
	SI_DICT,	/* -- || -- */
}
sgs_Instruction;


typedef struct funct_s
{
	char*	bytecode;
	int32_t	size;
	int16_t	instr_off;
	int8_t	gotthis;
}
funct;



struct _sgs_Variable
{
	int	refcount;
	uint8_t type;
	uint8_t redblue; /* red or blue? mark & sweep */
	uint8_t destroying; /* whether the variable is already in a process of destruction. for container circ. ref. handling. */
	union _sgs_Variable_data
	{
		/* 32/64 bit sizes, size of union isn't guaranteed to be max(all). */
		int32_t		B;	/* 4 */
		sgs_Integer	I;	/* 8 */
		sgs_Real	R;	/* 8 */
		StrBuf		S;	/* 12/16 */
		funct		F;	/* 12/16 */
		sgs_CFunc	C;	/* 4/8 */
		sgs_VarObj	O;	/* 8/16 */
	} data;
	sgs_Variable *prev, *next;
};

/*
sgs_Variable* sgsVM_VarCreate( SGS_CTX, int type );
void sgsVM_VarDestroy( SGS_CTX, sgs_Variable* var );
sgs_Variable* sgsVM_VarCreateString( SGS_CTX, const char* str, int32_t len );
*/
sgs_Variable* make_var( SGS_CTX, int type );
sgs_Variable* var_create_str( SGS_CTX, const char* str, int32_t len );
void destroy_var( SGS_CTX, sgs_Variable* var );
#define sgsVM_VarCreate make_var
#define sgsVM_VarDestroy destroy_var
#define sgsVM_VarCreateString var_create_str


int sgsVM_VarSize( sgs_Variable* var );
void sgsVM_VarDump( sgs_Variable* var );

void sgsVM_StackDump( SGS_CTX );

int sgsVM_ExecFn( SGS_CTX, const void* code, int32_t codesize, const void* data, int32_t datasize );


sgs_Variable* sgsVM_VarMake_Dict();

int sgsVM_RegStdLibs( SGS_CTX );


#endif /* SGS_PROC_H_INCLUDED */
