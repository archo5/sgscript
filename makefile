
ifdef SystemRoot
	RM = del /Q
	FixPath = $(subst /,\,$1)
	PLATFLAGS = -lkernel32
	BINEXT=.exe
	LIBEXT=.dll
else
	RM = rm -f
	FixPath = $1
	PLATFLAGS = 
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

SRCDIR=src
LIBDIR=lib
EXTDIR=ext
OUTDIR=bin
OBJDIR=obj


_DEPS = sgs_bcg.h sgs_cfg.h sgs_ctx.h sgs_fnt.h sgs_proc.h sgs_tok.h sgs_util.h sgs_xpc.h sgscript.h
DEPS = $(patsubst %,$(SRCDIR)/%,$(_DEPS))

_OBJ = sgs_bcg.o sgs_ctx.o sgs_fnt.o sgs_proc.o sgs_std.o sgs_stdL.o sgs_tok.o sgs_util.o sgs_xpc.o
OBJ = $(patsubst %,$(OBJDIR)/%,$(_OBJ))

# the library (default target)
$(LIBDIR)/libsgscript.a: $(OBJ)
	ar rcs $@ $(OBJ)
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

# the libraries
$(OUTDIR)/sgsjson$(LIBEXT): $(LIBDIR)/libsgscript.a $(EXTDIR)/sgsjson.c
	$(CC) -o $@ $(EXTDIR)/sgsjson.c -shared -lsgscript -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
# the tools
$(OUTDIR)/sgstest$(BINEXT): $(LIBDIR)/libsgscript.a $(EXTDIR)/sgstest.c $(OUTDIR)/sgsjson$(LIBEXT)
	$(CC) -o $@ $(EXTDIR)/sgstest.c -lsgscript -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsvm$(BINEXT): $(LIBDIR)/libsgscript.a $(EXTDIR)/sgsvm.c
	$(CC) -o $@ $(EXTDIR)/sgsvm.c -lsgscript -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsc$(BINEXT): $(LIBDIR)/libsgscript.a $(EXTDIR)/sgsc.c
	$(CC) -o $@ $(EXTDIR)/sgsc.c -lsgscript -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
.PHONY: tools
tools: $(OUTDIR)/sgsjson$(LIBEXT) $(OUTDIR)/sgstest$(BINEXT) $(OUTDIR)/sgsvm$(BINEXT) $(OUTDIR)/sgsc$(BINEXT)
.PHONY: test
test: $(OUTDIR)/sgstest$(BINEXT)
	$(OUTDIR)/sgstest

# other stuff
# - multithreaded testing
.PHONY: test_mt
test_mt: $(OUTDIR)/sgstest_mt$(BINEXT)
	$(OUTDIR)/sgstest_mt
$(OUTDIR)/sgstest_mt$(BINEXT): $(LIBDIR)/libsgscript.a
	$(CC) -o $@ examples/sgstest_mt.c -lsgscript -lm -lpthread $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)

# clean everything
.PHONY: clean
clean:
	$(RM) $(call FixPath,$(OBJDIR)/*.o $(LIBDIR)/*.a $(LIBDIR)/*.lib $(OUTDIR)/sgs*)
