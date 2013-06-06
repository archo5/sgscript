
ifdef SystemRoot
	RM = del /Q
	FixPath = $(subst /,\,$1)
	PLATFLAGS = -lkernel32
	SOCKLIBS = -lws2_32
	BINEXT=.exe
	LIBEXT=.dll
else
	RM = rm -f
	FixPath = $1
	PLATFLAGS = -ldl
	BINEXT=
	LIBEXT=.so
endif

CC=gcc
ifeq ($(mode),release)
	CFLAGS = -O3 -Wall
else
	mode = debug
	CFLAGS = -D_DEBUG -g -Wall
endif

ifneq ($(static),)
	LFLAGS = -lsgscript
	OUTLIB = libsgscript.a
	OUTFILE = $(LIBDIR)/libsgscript.a
else
	LFLAGS = $(OUTDIR)/sgscript$(LIBEXT)
	OUTLIB = sgscript$(LIBEXT)
	OUTFILE = $(OUTDIR)/sgscript$(LIBEXT)
endif

SRCDIR=src
LIBDIR=lib
EXTDIR=ext
OUTDIR=bin
OBJDIR=obj


_DEPS = sgs_cfg.h sgs_int.h sgs_util.h sgs_xpc.h sgscript.h
DEPS = $(patsubst %,$(SRCDIR)/%,$(_DEPS))

_OBJ = sgs_bcg.o sgs_ctx.o sgs_fnt.o sgs_proc.o sgs_std.o sgs_stdL.o sgs_tok.o sgs_util.o sgs_xpc.o
OBJ = $(patsubst %,$(OBJDIR)/%,$(_OBJ))

# the library (default target)
$(LIBDIR)/libsgscript.a: $(OBJ)
	ar rcs $@ $(OBJ)
$(OUTDIR)/sgscript$(LIBEXT): $(OBJ)
	gcc -o $@ -shared $(OBJ) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

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
	$(OUTDIR)/sgstest

.PHONY: apitest
apitest: $(OUTDIR)/sgsapitest$(BINEXT)
	$(OUTDIR)/sgsapitest

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
	objcopy --only-keep-debug "$(OUTDIR)/sgsexe$(BINEXT)" "$(OUTDIR)/sgsexe$(BINEXT).dbg"
	strip -s "$(OUTDIR)/sgsexe$(BINEXT)"
	objcopy --add-gnu-debuglink="$(OUTDIR)/sgsexe$(BINEXT).dbg" "$(OUTDIR)/sgsexe$(BINEXT)"

# clean everything
.PHONY: clean
clean:
	$(RM) $(call FixPath,$(OBJDIR)/*.o $(LIBDIR)/*.a $(LIBDIR)/*.lib $(OUTDIR)/sgs*)
