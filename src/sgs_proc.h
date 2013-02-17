
#ifndef SGS_PROC_H_INCLUDED
#define SGS_PROC_H_INCLUDED


#include "sgs_util.h"
#include "sgs_fnt.h"


#define CONSTENC( x ) ((x)|0x8000)
#define CONSTDEC( x ) ((x)&0x7fff)

typedef enum sgs_Instruction_e
{
	SI_NOP = 0,

	SI_PUSH,     /* (i16 src)               push register/constant */
	SI_PUSHN,    /* (u8 N)                  push N nulls */
	SI_POPN,     /* (u8 N)                  pop N items */
	SI_POPR,     /* (i16 out)               pop item to register */

	SI_RETN,     /* (u8 N)                  exit current frame of execution, preserve N output arguments */
	SI_JUMP,     /* (i16 off)               add to instruction pointer */
	SI_JMPT,     /* (i16 src, off)          jump (add to instr.ptr.) if true */
	SI_JMPF,     /* (i16 src, off)          jump (add to instr.ptr.) if false */
	SI_CALL,     /* (u8 args, exp, i16 src) call a variable */

	SI_FORPREP,  /* (i16 out, src)          retrieves the iterator to work the object */
	SI_FORNEXT,  /* (i16 oky, ost, iter)    retrieves pending output key/state from iterator */

	SI_GETVAR,   /* (i16 out, name)         <varname> => <value> */
	SI_SETVAR,   /* (i16 name, src)         <varname> <value> => set <value> to <varname> */
	SI_GETPROP,  /* (i16 out, var, name)    <var> <prop> => <var> */
	SI_SETPROP,  /* (i16 var, name, src)    <var> <prop> <value> => set a <prop> of <var> to <value> */
	SI_GETINDEX, /* -- || -- */
	SI_SETINDEX, /* -- || -- */

	/* operators */
	/*
		A: (i16 out, s1, s2)
		B: (i16 out, s1)
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
	SI_ARRAY,    /* (u8 args, i16 out) */
	SI_DICT,     /* -- || -- */
}
sgs_Instruction;


typedef uint32_t instr_t;

#define INSTR_OFF_OP  26
#define INSTR_OFF_A   18
#define INSTR_OFF_B   9
#define INSTR_OFF_C   0
#define INSTR_OFF_E   0

#define INSTR_MASK_OP 0x003f
#define INSTR_MASK_A  0x00ff
#define INSTR_MASK_B  0x01ff
#define INSTR_MASK_C  0x01ff
#define INSTR_MASK_E  0x0003ffff

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
#define INSTR_MAKE_EX( op, a, ex ) \
	( INSTR_MAKE_OP( op ) | INSTR_MAKE_A( a ) | INSTR_MAKE_E( ex ) )


typedef struct func_s
{
	LNTable lineinfo;
	StrBuf funcname;
	LineNum linenum;
	int32_t refcount;
	int32_t size;
	int16_t instr_off;
	int8_t  gotthis;
	int8_t  numargs;
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

typedef union _sgs_VarData
{
	int32_t     B;
	sgs_Integer I;
	sgs_Real    R;
	string_t*   S;
	func_t*     F;
	sgs_CFunc   C;
	sgs_VarObj* O;
}
sgs_VarData;


struct _sgs_Variable
{
	uint8_t     type;
	sgs_VarData data;
};


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

void vht_init( VHTable* vht );
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

int sgsVM_ExecFn( SGS_CTX, void* code, int32_t codesize, void* data, int32_t datasize, int clean, LNTable* T );
int sgsVM_VarCall( SGS_CTX, sgs_Variable* var, int args, int expect, int gotthis );


sgs_Variable* sgsVM_VarMake_Dict();

int sgsVM_RegStdLibs( SGS_CTX );


#endif /* SGS_PROC_H_INCLUDED */
