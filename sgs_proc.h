
#ifndef SGS_PROC_H_INCLUDED
#define SGS_PROC_H_INCLUDED


#include "sgs_util.h"
#include "sgs_fnt.h"


typedef enum sgs_Instruction_e
{
	SI_NOP = 0,

	SI_PUSHC,	/* push a constant */
	SI_PUSHS,	/* push (duplicate) a stack item */
	SI_PUSHN,	/* push null */
	SI_DUP,		/* duplicate top item on stack */

	SI_COPY,	/* copy data from -2 to -1 */
	SI_POP,		/* pop one item */
	SI_POPN,	/* pop N items */
	SI_POPS,	/* copy item to stack and pop it */
	SI_RETN,	/* exit current frame of execution, copy N output arguments */
	SI_JUMP,	/* add to instruction pointer */
	SI_JMPF,	/* jump (add to instr.ptr.) if false */
	SI_CALL,	/* call a variable */

	SI_GETVAR,	/* <varname> => <value> */
	SI_SETVAR,	/* <varname> <value> => set <value> to <varname> */
	SI_GETPROP,	/* <var> <prop> => <var> */
	SI_SETPROP,	/* <var> <prop> <value> => set a <prop> of <var> to <value> */
	SI_GETINDEX,
	SI_SETINDEX,

	/* operators */
	SI_CONCAT,
	SI_BOOL_AND,
	SI_BOOL_OR,
	SI_NEGATE,
	SI_BOOL_INV,
	SI_INVERT,

	SI_INC,
	SI_DEC,
	SI_ADD,
	SI_SUB,
	SI_MUL,
	SI_DIV,
	SI_MOD,

	SI_AND,
	SI_OR,
	SI_XOR,
	SI_LSH,
	SI_RSH,

	SI_SEQ,
	SI_SNEQ,
	SI_EQ,
	SI_NEQ,
	SI_LT,
	SI_GT,
	SI_LTE,
	SI_GTE,
}
sgs_Instruction;


typedef struct funct_s
{
	char*	bytecode;
	int32_t	instr_off;
	int32_t	size;
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


sgs_Variable* sgsVM_VarCreate( SGS_CTX, int type );
void sgsVM_VarDestroy( SGS_CTX, sgs_Variable* var );
sgs_Variable* sgsVM_VarCreateString( SGS_CTX, const char* str, int32_t len );


int sgsVM_VarSize( sgs_Variable* var );
void sgsVM_VarDump( sgs_Variable* var );

void sgsVM_StackDump( SGS_CTX );

int sgsVM_ExecFn( SGS_CTX, const void* code, int32_t codesize, const void* data, int32_t datasize );


sgs_Variable* sgsVM_VarMake_Dict();

int sgsVM_RegStdLibs( SGS_CTX );


#endif // SGS_PROC_H_INCLUDED
