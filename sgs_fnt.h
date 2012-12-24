
#ifndef SGS_FNT_H_INCLUDED
#define SGS_FNT_H_INCLUDED


#include "sgs_tok.h"

/*
	Function tree
*/

/* data */
#define SFT_IDENT	1
#define SFT_KEYWORD	2
#define SFT_CONST	3
/* expression parts */
#define SFT_OPER	4
#define SFT_OPER_P	5 /* post-op (inc/dec) */
#define SFT_FCALL	6
#define SFT_INDEX	7
/* statement data */
#define SFT_ARGMT	10
#define SFT_ARGLIST	11
#define SFT_VARLIST	12
#define SFT_GVLIST	13
#define SFT_EXPLIST	17
#define SFT_RETURN	18
/* statement types */
#define SFT_BLOCK	21
#define SFT_IFELSE	22
#define SFT_WHILE	23
#define SFT_DOWHILE	24
#define SFT_FOR		25
#define SFT_BREAK	26
#define SFT_CONT	27
#define SFT_FUNC	30

typedef struct _FTNode FTNode;
struct _FTNode
{
	TokenList token;
	FTNode* next;
	FTNode* child;
	short type;
};

void	sgsFT_Destroy( FTNode* tree );

FTNode*	sgsFT_Compile( SGS_CTX, TokenList tlist );
void	sgsFT_Dump( FTNode* tree );


#endif /* SGS_FNT_H_INCLUDED */
