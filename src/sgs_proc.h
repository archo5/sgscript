
#ifndef SGS_PROC_H_INCLUDED
#define SGS_PROC_H_INCLUDED


#ifndef _sgs_iStr
#  define _sgs_iStr string_s
#  define _sgs_iFunc func_s
#endif

#include "sgs_util.h"
#include "sgs_fnt.h"
#include "sgscript.h"


#define CONSTVAR( x ) ((x)&0x100)
#define CONSTENC( x ) ((x)|0x100)
#define CONSTDEC( x ) ((x)&0xff)

typedef enum sgs_Instruction_e
{
	SI_NOP = 0,

	SI_PUSH,     /* (B:src)                 push register/constant */
	SI_PUSHN,    /* (A:N)                   push N nulls */
	SI_POPN,     /* (A:N)                   pop N items */
	SI_POPR,     /* (A:out)                 pop item to register */

	SI_RETN,     /* (A:N)                   exit current frame of execution, preserve N output arguments */
	SI_JUMP,     /* (E:off)                 add to instruction pointer */
	SI_JMPT,     /* (C:src, E:off)          jump (add to instr.ptr.) if true */
	SI_JMPF,     /* (C:src, E:off)          jump (add to instr.ptr.) if false */
	SI_CALL,     /* (A:exp, B:args, C:src)  call a variable */

	SI_FORPREP,  /* (A:out, B:src)          retrieves the iterator to work the object */
	SI_FORNEXT,  /* (A:oky, B:ost, C:iter)  retrieves pending output key/state from iterator */

	SI_GETVAR,   /* (A:out, B:name)         <varname> => <value> */
	SI_SETVAR,   /* (B:name, C:src)         <varname> <value> => set <value> to <varname> */
	SI_GETPROP,  /* (A:out, B:var, C:name)  <var> <prop> => <var> */
	SI_SETPROP,  /* (A:var, B:name, C:src)  <var> <prop> <value> => set a <prop> of <var> to <value> */
	SI_GETINDEX, /* -- || -- */
	SI_SETINDEX, /* -- || -- */

	/* operators */
	/*
		A: (A:out, B:s1, C:s2)
		B: (A:out, B:s1)
	*/
	SI_SET,      /* B */
	SI_CLONE,
	SI_CONCAT,   /* A */
	SI_NEGATE,   /* B */
	SI_BOOL_INV,
	SI_INVERT,

	SI_INC,      /* B */
	SI_DEC,
	SI_ADD,      /* A */
	SI_SUB,
	SI_MUL,
	SI_DIV,
	SI_MOD,

	SI_AND,      /* A */
	SI_OR,
	SI_XOR,
	SI_LSH,
	SI_RSH,

	SI_SEQ,      /* A */
	SI_SNEQ,
	SI_EQ,
	SI_NEQ,
	SI_LT,
	SI_GTE,
	SI_GT,
	SI_LTE,

	/* specials */
	SI_ARRAY,    /* (A:out, B:args) */
	SI_DICT,     /* -- || -- */
}
sgs_Instruction;


typedef uint32_t instr_t;

/*
	instruction data: 32 bits
	OOOOOOAAAAAAAABBBBBBBBBCCCCCCCCC
	OOOOOOEEEEEEEEEEEEEEEEECCCCCCCCC
*/

#define INSTR_SIZE    4

#define INSTR_OFF_OP  0
#define INSTR_OFF_A   6
#define INSTR_OFF_B   14
#define INSTR_OFF_C   23
#define INSTR_OFF_E   6

#define INSTR_MASK_OP 0x003f /* OP: 6 bits */
#define INSTR_MASK_A  0x00ff /* A:  8 bits */
#define INSTR_MASK_B  0x01ff /* B:  9 bits */
#define INSTR_MASK_C  0x01ff /* C:  9 bits */
#define INSTR_MASK_E  0x0001ffff

#define INSTR_GET_OP( x )  ( ( ( x ) >> INSTR_OFF_OP ) & INSTR_MASK_OP )
#define INSTR_GET_A( x )   ( ( ( x ) >> INSTR_OFF_A  ) & INSTR_MASK_A  )
#define INSTR_GET_B( x )   ( ( ( x ) >> INSTR_OFF_B  ) & INSTR_MASK_B  )
#define INSTR_GET_C( x )   ( ( ( x ) >> INSTR_OFF_C  ) & INSTR_MASK_C  )
#define INSTR_GET_E( x )   ( ( ( x ) >> INSTR_OFF_E  ) & INSTR_MASK_E  )

#define INSTR_MAKE_OP( x ) ( ( ( x ) & INSTR_MASK_OP ) << INSTR_OFF_OP )
#define INSTR_MAKE_A( x )  ( ( ( x ) & INSTR_MASK_A  ) << INSTR_OFF_A  )
#define INSTR_MAKE_B( x )  ( ( ( x ) & INSTR_MASK_B  ) << INSTR_OFF_B  )
#define INSTR_MAKE_C( x )  ( ( ( x ) & INSTR_MASK_C  ) << INSTR_OFF_C  )
#define INSTR_MAKE_E( x )  ( ( ( x ) & INSTR_MASK_E  ) << INSTR_OFF_E  )

#define INSTR_MAKE( op, a, b, c ) \
	( INSTR_MAKE_OP( op ) | INSTR_MAKE_A( a ) | INSTR_MAKE_B( b ) | INSTR_MAKE_C( c ) )
#define INSTR_MAKE_EX( op, ex, c ) \
	( INSTR_MAKE_OP( op ) | INSTR_MAKE_E( ex ) | INSTR_MAKE_C( c ) )
#define INSTR_RECOMB_E( a, b ) ( ( ( b ) << 8 ) | ( a ) )


typedef struct func_s
{
	int32_t refcount;
	int32_t size;
	int16_t instr_off;
	int8_t  gotthis;
	int8_t  numargs;
	LineNum linenum;
	uint16_t* lineinfo;
	MemBuf  funcname;
	MemBuf  filename;
}
func_t;
#define func_consts( pfn )   ((sgs_Variable*)(((char*)(pfn))+sizeof(func_t)))
#define func_bytecode( pfn ) ((instr_t*)(((char*)(pfn))+sizeof(func_t)+pfn->instr_off))

typedef struct string_s
{
	int32_t refcount;
	int32_t mem;
	int32_t size;
}
string_t;
#define str_cstr( pstr ) (((char*)(pstr))+sizeof(string_t))
#define var_cstr( var ) str_cstr( (var)->data.S )

#define object_t sgs_VarObj


/* hash table for variables */
typedef struct _VHTableVar
{
	sgs_Variable var;
	char*        str;
	int          size;
}
VHTableVar;
typedef struct _VHTable
{
	HashTable   ht;
	VHTableVar* vars;
	int32_t     mem;
}
VHTable;

void vht_init( VHTable* vht, SGS_CTX );
void vht_free( VHTable* vht, SGS_CTX );
VHTableVar* vht_get( VHTable* vht, const char* key, int32_t size );
void vht_set( VHTable* vht, const char* key, int32_t size, sgs_Variable* var, SGS_CTX );
int vht_unset( VHTable* vht, const char* key, int32_t size, SGS_CTX );
#define vht_size( T ) ((T)->ht.load)


/* VM interface */
void var_create_str( SGS_CTX, sgs_Variable* out, const char* str, int32_t len );
void var_destroy_object( SGS_CTX, object_t* O );
#define sgsVM_VarCreateString var_create_str
#define sgsVM_VarDestroyObject var_destroy_object
int vm_convert_stack( SGS_CTX, int item, int type );


int sgsVM_VarSize( sgs_Variable* var );
void sgsVM_VarDump( sgs_Variable* var );

void sgsVM_StackDump( SGS_CTX );

int sgsVM_ExecFn( SGS_CTX, void* code, int32_t codesize, void* data, int32_t datasize, int clean, uint16_t* T );
int sgsVM_VarCall( SGS_CTX, sgs_Variable* var, int args, int expect, int gotthis );


sgs_Variable* sgsVM_VarMake_Dict();

int sgsVM_RegStdLibs( SGS_CTX );


#endif /* SGS_PROC_H_INCLUDED */
