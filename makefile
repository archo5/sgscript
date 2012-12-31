
SRCDIR=src
LIBDIR=lib
EXTDIR=ext
OUTDIR=bin
OBJDIR=obj

CC=gcc
CFLAGS=

_DEPS = sgs_bcg.h sgs_cfg.h sgs_ctx.h sgs_fnt.h sgs_proc.h sgs_tok.h sgs_util.h sgs_xpc.h sgscript.h
DEPS = $(patsubst %,$(SRCDIR)/%,$(_DEPS))

_OBJ = sgs_bcg.o sgs_ctx.o sgs_fnt.o sgs_proc.o sgs_std.o sgs_tok.o sgs_util.o
OBJ = $(patsubst %,$(OBJDIR)/%,$(_OBJ))


$(LIBDIR)/libsgscript.a: $(OBJ)
	ar rcs $@ $(OBJ)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: test
test: sgstest
	$(OUTDIR)/sgstest

.PHONY: sgstest
sgstest: $(LIBDIR)/libsgscript.a
	$(CC) -o $(OUTDIR)/$@ $(EXTDIR)/sgstest.c -lsgscript -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJDIR)/*.o $(LIBDIR)/*.a $(LIBDIR)/*.lib $(OUTDIR)/sgs*
