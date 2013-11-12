
CC=gcc
COMMONFLAGS=-Wall
SRCDIR=src
LIBDIR=lib
EXTDIR=ext
OUTDIR=bin
OBJDIR=obj

ifdef SystemRoot
	RM = del /Q
	FixPath = $(subst /,\,$1)
	CPLATFLAGS =
	PLATFLAGS = -lkernel32
	SOCKLIBS = -lws2_32
	BINEXT=.exe
	LIBEXT=.dll
	LIBPFX=
else
	RM = rm -f
	FixPath = $1
	CPLATFLAGS = -fPIC
	PLATFLAGS = -ldl -lrt -Wl,-rpath,'$$ORIGIN' -Wl,-z,origin
	BINEXT=
	LIBEXT=.so
	LIBPFX=lib
endif

ifeq ($(arch),64)
	ARCHFLAGS= -m64
else
	ifeq ($(arch),32)
		ARCHFLAGS= -m32
	else
		ARCHFLAGS=
	endif
endif

ifeq ($(mode),release)
	CFLAGS = -O3 $(COMMONFLAGS) $(ARCHFLAGS) $(CPLATFLAGS)
else
	mode = debug
	CFLAGS = -D_DEBUG -g $(COMMONFLAGS) $(ARCHFLAGS) $(CPLATFLAGS)
endif

ifneq ($(jit),)
	_XDEPS =
	_XOBJ = sgs_jit.o
	_XFLAGS = -DSGS_JIT=1
else
	_XDEPS =
	_XOBJ =
	_XFLAGS =
endif

ifneq ($(static),)
	PREFLAGS = -DBUILDING_SGS=1 $(_XFLAGS)
	LFLAGS = -lsgscript
	OUTLIB = libsgscript.a
	OUTFILE = $(LIBDIR)/libsgscript.a
else
	PREFLAGS = -DBUILDING_SGS=1 -DSGS_DLL=1 $(_XFLAGS)
	LFLAGS = -L$(OUTDIR) -lsgscript
	OUTLIB = $(LIBPFX)sgscript$(LIBEXT)
	OUTFILE = $(OUTDIR)/$(LIBPFX)sgscript$(LIBEXT)
endif


_DEPS = sgs_cfg.h sgs_int.h sgs_util.h sgs_xpc.h sgscript.h sgs_regex.h $(_XDEPS)
DEPS = $(patsubst %,$(SRCDIR)/%,$(_DEPS))

_OBJ = sgs_bcg.o sgs_ctx.o sgs_fnt.o sgs_proc.o sgs_std.o sgs_stdL.o sgs_tok.o sgs_util.o sgs_xpc.o sgs_regex.o $(_XOBJ)
OBJ = $(patsubst %,$(OBJDIR)/%,$(_OBJ))

# the library (default target)
.PHONY: make
make: $(OUTFILE)

$(LIBDIR)/libsgscript.a: $(OBJ)
	ar rcs $@ $(OBJ)
$(OUTDIR)/$(LIBPFX)sgscript$(LIBEXT): $(OBJ)
	$(CC) $(PREFLAGS) -o $@ -shared $(OBJ) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(DEPS)
	$(CC) $(PREFLAGS) -c -o $@ $< $(CFLAGS)

# the libraries
$(OUTDIR)/sgsxgmath$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgsxgmath.c $(OUTFILE) $(EXTDIR)/sgsxgmath.h
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgsxgmath.c -shared $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsjson$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgsjson.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgsjson.c -shared $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgspproc$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgspproc.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgspproc.c -shared $(LFLAGS) -lpthread -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgssockets$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgssockets.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgssockets.c -shared $(LFLAGS) $(SOCKLIBS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsmeta$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgsmeta.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgsmeta.c -shared $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)

# the tools
$(OUTDIR)/sgstest$(BINEXT): $(OUTFILE) $(EXTDIR)/sgstest.c $(OUTDIR)/sgsjson$(LIBEXT)
	$(CC) -o $@ $(EXTDIR)/sgstest.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsapitest$(BINEXT): $(OUTFILE) $(EXTDIR)/sgsapitest.c $(OUTDIR)/sgsjson$(LIBEXT)
	$(CC) -o $@ $(EXTDIR)/sgsapitest.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsvm$(BINEXT): $(OUTFILE) $(EXTDIR)/sgsvm.c $(EXTDIR)/sgs_idbg.c $(EXTDIR)/sgs_prof.c
	$(CC) -o $@ $(EXTDIR)/sgsvm.c $(EXTDIR)/sgs_idbg.c $(EXTDIR)/sgs_prof.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsc$(BINEXT): $(OUTFILE) $(EXTDIR)/sgsc.c
	$(CC) -o $@ $(EXTDIR)/sgsc.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)

# library/tool aliases
.PHONY: xgmath
.PHONY: json
.PHONY: pproc
.PHONY: sockets
.PHONY: meta
.PHONY: build_test
.PHONY: build_apitest
.PHONY: vm
.PHONY: c
.PHONY: test
.PHONY: apitest
xgmath: $(OUTDIR)/sgsxgmath$(LIBEXT)
json: $(OUTDIR)/sgsjson$(LIBEXT)
pproc: $(OUTDIR)/sgspproc$(LIBEXT)
sockets: $(OUTDIR)/sgssockets$(LIBEXT)
meta: $(OUTDIR)/sgsmeta$(LIBEXT)
build_test: $(OUTDIR)/sgstest$(BINEXT)
build_apitest: $(OUTDIR)/sgsapitest$(BINEXT)
vm: $(OUTDIR)/sgsvm$(BINEXT)
c: $(OUTDIR)/sgsc$(BINEXT)

# test tool aliases
test: $(OUTDIR)/sgstest$(BINEXT)
	$(OUTDIR)/sgstest
apitest: $(OUTDIR)/sgsapitest$(BINEXT)
	$(OUTDIR)/sgsapitest

.PHONY: tools
tools: xgmath json pproc sockets meta build_test build_apitest vm c

# other stuff
# - multithreaded testing
.PHONY: test_mt
test_mt: $(OUTDIR)/sgstest_mt$(BINEXT)
	$(OUTDIR)/sgstest_mt
$(OUTDIR)/sgstest_mt$(BINEXT): $(LIBDIR)/libsgscript.a
	$(CC) -o $@ examples/sgstest_mt.c $(LFLAGS) -lm -lpthread $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
# - sgs2exe tool
.PHONY: sgsexe
sgsexe: $(LIBDIR)/libsgscript.a $(EXTDIR)/sgsexe.c
	$(CC) -o $(OUTDIR)/sgsexe$(BINEXT) $(EXTDIR)/sgsexe.c $(LIBDIR)/libsgscript.a -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
	copy /B $(OUTDIR)\sgsexe$(BINEXT) + $(EXTDIR)\stubapp.bin $(OUTDIR)\sgsexe.tmp
	del /Q $(OUTDIR)\sgsexe$(BINEXT)
	cmd /c move /Y $(OUTDIR)\sgsexe.tmp $(OUTDIR)\sgsexe$(BINEXT)

# clean build data
.PHONY: clean_obj
clean_obj:
	$(RM) $(call FixPath,$(OBJDIR)/*.o)

.PHONY: clean_objbin
clean_objbin:
	$(RM) $(call FixPath,$(OBJDIR)/*.o $(OUTDIR)/sgs* $(OUTDIR)/libsgs*)

.PHONY: clean
clean:
	$(RM) $(call FixPath,$(OBJDIR)/*.o $(LIBDIR)/*.a $(LIBDIR)/*.lib $(OUTDIR)/sgs* $(OUTDIR)/libsgs*)
