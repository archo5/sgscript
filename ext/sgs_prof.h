
#ifndef SGS_PROF_H_INCLUDED
#define SGS_PROF_H_INCLUDED


#ifdef __cplusplus
extern "C" {
#endif


#ifndef HEADER_SGSCRIPT_H
# define HEADER_SGSCRIPT_H <sgscript.h>
#endif
#include HEADER_SGSCRIPT_H
#ifndef HEADER_SGS_UTIL_H
# define HEADER_SGS_UTIL_H <sgs_util.h>
#endif
#include HEADER_SGS_UTIL_H

typedef
struct _sgs_Prof
{
	SGS_CTX;
	sgs_HookFunc hfn;
	void* hctx;

	int mode;
	/* mode 1 / 3 */
	sgs_MemBuf keytmp;
	sgs_MemBuf timetmp;
	sgs_VHTable timings;
	/* mode 2 */
	int prev;
	double* ictrs;
	uint32_t* iexcs;
	int32_t instr;
	double starttime;
}
sgs_Prof;

#define SGS_PROF_FUNCTIME 1
#define SGS_PROF_OPTIME   2
#define SGS_PROF_MEMUSAGE 3

SGS_APIFUNC void sgs_ProfInit( SGS_CTX, sgs_Prof* P, int mode );
SGS_APIFUNC void sgs_ProfClose( sgs_Prof* P );
SGS_APIFUNC void sgs_ProfDump( sgs_Prof* P );


#ifdef __cplusplus
}
#endif

#endif
