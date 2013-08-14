
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
	LDPREP=
else
	RM = rm -f
	FixPath = $1
	CPLATFLAGS = -fPIC
	PLATFLAGS = -ldl
	BINEXT=
	LIBEXT=.so
	LIBPFX=lib
	LDPREP=LD_LIBRARY_PATH=./bin
endif

ifeq ($(mode),release)
	CFLAGS = -O3 $(COMMONFLAGS) $(CPLATFLAGS)
else
	mode = debug
	CFLAGS = -D_DEBUG -g $(COMMONFLAGS) $(CPLATFLAGS)
endif

ifneq ($(static),)
	PREFLAGS = -DBUILDING_SGS=1
	LFLAGS = -lsgscript
	OUTLIB = libsgscript.a
	OUTFILE = $(LIBDIR)/libsgscript.a
else
	PREFLAGS = -DBUILDING_SGS=1 -DSGS_DLL=1
	LFLAGS = -Lbin -lsgscript
	OUTLIB = $(LIBPFX)sgscript$(LIBEXT)
	OUTFILE = $(OUTDIR)/$(LIBPFX)sgscript$(LIBEXT)
endif


_DEPS = sgs_cfg.h sgs_int.h sgs_util.h sgs_xpc.h sgscript.h sgs_regex.h
DEPS = $(patsubst %,$(SRCDIR)/%,$(_DEPS))

_OBJ = sgs_bcg.o sgs_ctx.o sgs_fnt.o sgs_proc.o sgs_std.o sgs_stdL.o sgs_tok.o sgs_util.o sgs_xpc.o sgs_regex.o
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
$(OUTDIR)/sgsjson$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgsjson.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgsjson.c -shared $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgspproc$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgspproc.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgspproc.c -shared $(LFLAGS) -lpthread -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgssockets$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgssockets.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgssockets.c -shared $(LFLAGS) $(SOCKLIBS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
# the tools
$(OUTDIR)/sgstest$(BINEXT): $(OUTFILE) $(EXTDIR)/sgstest.c $(OUTDIR)/sgsjson$(LIBEXT)
	$(CC) -o $@ $(EXTDIR)/sgstest.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsapitest$(BINEXT): $(OUTFILE) $(EXTDIR)/sgsapitest.c $(OUTDIR)/sgsjson$(LIBEXT)
	$(CC) -o $@ $(EXTDIR)/sgsapitest.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsvm$(BINEXT): $(OUTFILE) $(EXTDIR)/sgsvm.c $(EXTDIR)/sgs_idbg.c $(EXTDIR)/sgs_prof.c
	$(CC) -o $@ $(EXTDIR)/sgsvm.c $(EXTDIR)/sgs_idbg.c $(EXTDIR)/sgs_prof.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsc$(BINEXT): $(OUTFILE) $(EXTDIR)/sgsc.c
	$(CC) -o $@ $(EXTDIR)/sgsc.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)

.PHONY: tools
tools: $(OUTDIR)/sgsjson$(LIBEXT) \
		$(OUTDIR)/sgspproc$(LIBEXT) \
		$(OUTDIR)/sgssockets$(LIBEXT) \
		$(OUTDIR)/sgstest$(BINEXT) \
		$(OUTDIR)/sgsapitest$(BINEXT) \
		$(OUTDIR)/sgsvm$(BINEXT) \
		$(OUTDIR)/sgsc$(BINEXT)


.PHONY: test
test: $(OUTDIR)/sgstest$(BINEXT)
	$(LDPREP) $(OUTDIR)/sgstest

.PHONY: apitest
apitest: $(OUTDIR)/sgsapitest$(BINEXT)
	$(LDPREP) $(OUTDIR)/sgsapitest

# other stuff
# - multithreaded testing
.PHONY: test_mt
test_mt: $(OUTDIR)/sgstest_mt$(BINEXT)
	$(LDPREP) $(OUTDIR)/sgstest_mt
$(OUTDIR)/sgstest_mt$(BINEXT): $(LIBDIR)/libsgscript.a
	$(CC) -o $@ examples/sgstest_mt.c $(LFLAGS) -lm -lpthread $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
# - sgs2exe tool
.PHONY: sgsexe
sgsexe: $(LIBDIR)/libsgscript.a $(EXTDIR)/sgsexe.c
	$(CC) -o $(OUTDIR)/sgsexe$(BINEXT) $(EXTDIR)/sgsexe.c $(LIBDIR)/libsgscript.a -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
	copy /B $(OUTDIR)\sgsexe$(BINEXT) + $(EXTDIR)\stubapp.bin $(OUTDIR)\sgsexe.tmp
	del /Q $(OUTDIR)\sgsexe$(BINEXT)
	cmd /c move /Y $(OUTDIR)\sgsexe.tmp $(OUTDIR)\sgsexe$(BINEXT)

# clean everything
.PHONY: clean
clean:
	$(RM) $(call FixPath,$(OBJDIR)/*.o $(LIBDIR)/*.a $(LIBDIR)/*.lib $(OUTDIR)/sgs* $(OUTDIR)/libsgs*)
