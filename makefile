# SGScript makefile


# Additional compilation info:
# CC (compiler) can be specified in the `make` command
# For Android, together with some android-*** target, pass this:
# CC="<some-prebuilt-gcc/clang> --sysroot=NDK_ROOT\platforms\<platform>\<arch>"


# MAKE
comma := ,
space :=
space +=


# BASIC VARIABLES
ifeq ($(CC),cc) # no such compiler on Windows
	CC=
endif
ifeq ($(CC),)
	CC=gcc
endif
ifeq ($(CXX),)
	CXX=g++
endif


# UTILITIES
cOS=os_unknown
cARCH=arch_unknown
cCOMPILER=compiler_unknown
ifeq ($(OS),Windows_NT)
	fnREMOVE_ALL = del /S /Q
	fnCOPY_FILE = copy
	fnFIX_PATH = $(subst /,\,$1)
	cOS=windows
	ifeq ($(PROCESSOR_ARCHITECTURE),x86)
		cARCH=x86
	endif
	ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
		cARCH=x64
	endif
else
	fnREMOVE_ALL = rm -rf
	fnCOPY_FILE = cp
	fnFIX_PATH = $1
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		cOS=linux
	endif
	ifeq ($(UNAME_S),Darwin)
		cOS=osx
	endif
	UNAME_P := $(shell uname -p)
	ifneq ($(filter %86,$(UNAME_P)),)
		cARCH=x86
	endif
	ifeq ($(UNAME_P),x86_64)
		cARCH=x64
	endif
	ifneq ($(filter arm%,$(UNAME_P)),)
		cARCH=arm
	endif
endif
CC_V := $(shell $(CC) -v 2>&1)
ifneq ($(findstring clang,$(CC_V)),)
	cCOMPILER=clang
endif
ifneq ($(findstring gcc,$(CC_V)),)
	cCOMPILER=gcc
endif
cOS_ARCH=$(cOS)-$(cARCH)


ifeq ($(target),)
	target=$(cOS_ARCH)
endif
target_os=$(word 1,$(subst -, ,$(target)))
target_arch=$(word 2,$(subst -, ,$(target)))
ifeq ($(target_os),)
$(error Target OS not specified (windows/linux/osx))
endif
ifeq ($(target_arch),)
$(error Target CPU type not specified (x86/x64/arm/..))
endif
cIF_RELEASE=$(findstring release,$(mode))
fnIF_RELEASE=$(if $(cIF_RELEASE),$1,$2)
fnIF_OS=$(if $(findstring $1,$(target_os)),$2,$3)
fnIF_ARCH=$(if $(findstring $1,$(target_arch)),$2,$3)
fnIF_OS_ARCH=$(if $(findstring $1,$(target)),$2,$3)
fnIF_COMPILER=$(if $(findstring $1,$(cCOMPILER)),$2,$3)


# PLATFORM SPECIFICS
ifeq ($(target_os),windows)
	BINEXT=.exe
	LIBPFX=
	LIBEXT=.dll
else
	BINEXT=
	LIBPFX=lib
	LIBEXT=.so
endif


# APP FLAGS
OUTFILE_STATIC=lib/libsgscript.a
OUTFILE_DYNAMIC=bin/$(LIBPFX)sgscript$(LIBEXT)
OUTFLAGS_STATIC=-Llib -lsgscript
OUTFLAGS_DYNAMIC=-Lbin -lsgscript
ifneq ($(static),)
	PREFLAGS=-DBUILDING_SGS=1
	OUTFILE=$(OUTFILE_STATIC)
	OUTFLAGS=$(OUTFLAGS_STATIC)
else
	PREFLAGS=-DBUILDING_SGS=1 -DSGS_DLL=1
	OUTFILE=$(OUTFILE_DYNAMIC)
	OUTFLAGS=$(OUTFLAGS_DYNAMIC)
endif
CFLAGS=-fwrapv -Wall -Wconversion -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align \
	$(call fnIF_RELEASE,-O3,-D_DEBUG -g) $(call fnIF_COMPILER gcc,-static-libgcc,) \
	$(call fnIF_ARCH,x86,-m32,$(call fnIF_ARCH,x64,-m64,)) -Isrc \
	$(call fnIF_OS,windows,,-fPIC -D_FILE_OFFSET_BITS=64) \
	$(call fnIF_OS,android,-DSGS_PF_ANDROID,)
COMFLAGS=$(CFLAGS)
BINFLAGS=$(CFLAGS) $(OUTFLAGS) -lm \
	$(call fnIF_OS,android,-ldl -Wl$(comma)-rpath$(comma)'$$ORIGIN' -Wl$(comma)-z$(comma)origin,) \
	$(call fnIF_OS,windows,-lkernel32,) \
	$(call fnIF_OS,osx,-ldl -Wl$(comma)-rpath$(comma)'$$ORIGIN') \
	$(call fnIF_OS,linux,-ldl -lrt -Wl$(comma)-rpath$(comma)'$$ORIGIN' -Wl$(comma)-z$(comma)origin,)
MODULEFLAGS=-DSGS_COMPILE_MODULE $(BINFLAGS) -shared
EXEFLAGS=$(BINFLAGS)
THREADLIBS=$(call fnIF_OS,windows,,$(call fnIF_OS,android,,-lpthread))


# BUILD INFO
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),clean_obj)
ifneq ($(MAKECMDGOALS),clean_objbin)
$(info -------------------)
$(info SGScript build info)
$(info -------------------)
$(info OS - $(cOS))
$(info ARCH - $(cARCH))
$(info COMPILER - $(cCOMPILER))
$(info TARGET - $(target_os)/$(target_arch))
$(info MODE - $(call fnIF_RELEASE,release,debug))
$(info OUT.LIB. - $(OUTFILE))
$(info TODO - $(MAKECMDGOALS))
$(info -------------------)
endif
endif
endif


# TARGETS
## the library (default target)
.PHONY: make
make: $(OUTFILE)

DEPS = $(patsubst %,src/%.h,sgs_cfg sgs_int sgs_regex sgs_util sgs_xpc sgscript)
OBJ = $(patsubst %,obj/sgs_%.o,bcg ctx fnt proc regex std stdL tok util xpc)

lib/libsgscript.a: $(OBJ)
	ar rcs $@ $(OBJ)
bin/$(LIBPFX)sgscript$(LIBEXT): $(OBJ)
	$(CC) $(PREFLAGS) -o $@ $(OBJ) $(CFLAGS) -shared -lm
obj/%.o: src/%.c $(DEPS)
	$(CC) $(PREFLAGS) -c -o $@ $< $(COMFLAGS)

## the libraries
bin/sgsxgmath$(LIBEXT): ext/sgsxgmath.c ext/sgsxgmath.h $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS)
bin/sgsjson$(LIBEXT): ext/sgsjson.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS)
bin/sgspproc$(LIBEXT): ext/sgspproc.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(THREADLIBS)
bin/sgssockets$(LIBEXT): ext/sgssockets.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(call fnIF_OS,windows,-lws2_32,)
bin/sgsmeta$(LIBEXT): ext/sgsmeta.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS)

## the tools
bin/sgstest$(BINEXT): ext/sgstest.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)
bin/sgsapitest$(BINEXT): ext/sgsapitest.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)
bin/sgsvm$(BINEXT): ext/sgsvm.c ext/sgs_idbg.c ext/sgs_prof.c $(OUTFILE)
	$(CC) -o $@ $(subst $(OUTFILE),,$^) $(EXEFLAGS)
bin/sgsc$(BINEXT): ext/sgsc.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)

## library/tool aliases
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
xgmath: bin/sgsxgmath$(LIBEXT)
json: bin/sgsjson$(LIBEXT)
pproc: bin/sgspproc$(LIBEXT)
sockets: bin/sgssockets$(LIBEXT)
meta: bin/sgsmeta$(LIBEXT)
build_test: bin/sgstest$(BINEXT) json xgmath
build_apitest: bin/sgsapitest$(BINEXT)
vm: bin/sgsvm$(BINEXT)
c: bin/sgsc$(BINEXT)

# test tool aliases
test: build_test
	bin/sgstest
apitest: build_apitest
	bin/sgsapitest

.PHONY: tools
tools: xgmath json pproc sockets meta build_test build_apitest vm c

## other stuff
## - cppbc testing
.PHONY: cppbctest
.PHONY: build_cppbctest
build_cppbctest: bin/sgscppbctest$(BINEXT)
cppbctest: build_cppbctest
	bin/sgscppbctest
bin/sgscppbctest$(BINEXT): ext/sgscppbctest.cpp obj/cppbc_test.cpp ext/sgscppbctest.h ext/cppbc/sgs_cppbc.h $(OUTFILE)
	$(CXX) -o $@ $< $(word 2,$^) $(EXEFLAGS) -I.
obj/cppbc_test.cpp: ext/sgscppbctest.h
	bin/sgsvm -p ext/cppbc/cppbc.sgs $< $@
## - cppbind testing
.PHONY: cppbindtest
.PHONY: build_cppbindtest
build_cppbindtest: bin/sgscppbindtest$(BINEXT)
cppbindtest: build_cppbindtest
	bin/sgscppbindtest
bin/sgscppbindtest$(BINEXT): ext/cpp/cppbind_example.cpp $(OUTFILE)
	$(CXX) -o $@ $< $(EXEFLAGS)
## - multithreaded testing
.PHONY: mttest
.PHONY: build_mttest
build_mttest: bin/sgstest_mt$(BINEXT)
mttest: build_mttest
	bin/sgstest_mt
bin/sgstest_mt$(BINEXT): examples/sgstest_mt.c $(OUTFILE)
	$(CC) -o $@ $^ $(EXEFLAGS) $(THREADLIBS)
## - sgs2exe tool
.PHONY: sgsexe
sgsexe: ext/sgsexe.c $(OUTFILE_STATIC)
	$(CC) -o bin/sgsexe.exe $^ $(CFLAGS) -lm
	copy /B bin\sgsexe.exe + ext\stubapp.bin bin\sgsexe.tmp
	del /Q bin\sgsexe.exe
	cmd /c move /Y bin\sgsexe.tmp bin\sgsexe.exe

## clean build data
.PHONY: clean_obj
clean_obj:
	$(fnREMOVE_ALL) $(call fnFIX_PATH,obj/*.o)

.PHONY: clean_objbin
clean_objbin:
	$(fnREMOVE_ALL) $(call fnFIX_PATH,obj/*.o bin/sgs* bin/libsgs*)

.PHONY: clean
clean:
	$(fnREMOVE_ALL) $(call fnFIX_PATH,obj/*.o lib/*.a lib/*.lib bin/sgs* bin/libsgs*)





ifeq (none,)

# APP FLAGS
CFLAGS = -D_DEBUG -g -Wall -Wno-comment -DBUILDING_SGS_SDL
COMPATHS = -I/usr/local/opt/freetype/include/freetype2 -Isgscript/src -Isgscript/ext
LIBFLAGS = -Wl,-rpath,'@executable_path/.' -Lbin -lsgs-sdl
SS_LIB_OBJ = $(patsubst %,obj/ss_%.o,main script sdl render image render_gl)
SS_LIB_FLAGS = $(SS_LIB_OBJ) $(CFLAGS) -framework OpenGL -lfreetype -lm \
	-Wl,-rpath,'@executable_path/.' sgscript/bin/sgsxgmath.so \
	-lfreeimage -shared -lSDL2main -lSDL2 -Lsgscript/bin -lsgscript
SS_LAUNCHER_FLAGS = src/ss_launcher.c $(COMPATHS) $(LIBFLAGS) $(CFLAGS)
SS_INSTALL_TOOL = install_name_tool \
	-change bin/libsgs-sdl.so @rpath/libsgs-sdl.so \
	-change bin/sgsxgmath.so @rpath/sgsxgmath.so \
	-change bin/libsgscript.so @rpath/libsgscript.so



# Target platform control
# For most sane platforms, use CC=gcc (default) or clang
# For Android, use <some-prebuilt-gcc/clang> -DSGS_PF_ANDROID \
#   --sysroot=NDK_ROOT\platforms\<platform>\<arch>
# .. and uncomment this line:
# PLATFORM=android
CC=gcc
CXX=g++

COMMONFLAGS_CI=-fwrapv -Wall -Wconversion -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align

ifeq ($(findstring clang,$(shell $(CC) -v 2>&1)),clang)
	COMMONFLAGS=$(COMMONFLAGS_CI)
else
	COMMONFLAGS=$(COMMONFLAGS_CI) -static-libgcc
endif

SRCDIR=src
LIBDIR=lib
EXTDIR=ext
OUTDIR=bin
OBJDIR=obj

ifeq ($(PLATFORM),android)
	CPLATFLAGS = -fPIC
	PLATFLAGS = -ldl -Wl,-rpath,'$$ORIGIN' -Wl,-z,origin
	BINEXT=
	LIBEXT=.so
	LIBPFX=lib
else
ifdef SystemRoot
	CPLATFLAGS =
	PLATFLAGS = -lkernel32
	SOCKLIBS = -lws2_32
	BINEXT=.exe
	LIBEXT=.dll
	LIBPFX=
else
	CPLATFLAGS = -fPIC -D_FILE_OFFSET_BITS=64
	ifeq ($(shell uname -s),Darwin)
		PLATFLAGS = -ldl -Wl,-rpath,'$$ORIGIN'
	else
		PLATFLAGS = -ldl -lrt -Wl,-rpath,'$$ORIGIN' -Wl,-z,origin
	endif
	THREADLIBS = -lpthread
	BINEXT=
	LIBEXT=.so
	LIBPFX=lib
endif
endif

ifdef SystemRoot
	RM = del /Q
	FixPath = $(subst /,\,$1)
else
	RM = rm -rf
	FixPath = $1
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


_DEPS = sgs_cfg.h sgs_int.h sgs_regex.h sgs_util.h sgs_xpc.h sgscript.h $(_XDEPS)
DEPS = $(patsubst %,$(SRCDIR)/%,$(_DEPS))

_OBJ = sgs_bcg.o sgs_ctx.o sgs_fnt.o sgs_proc.o sgs_regex.o sgs_std.o sgs_stdL.o sgs_tok.o sgs_util.o sgs_xpc.o $(_XOBJ)
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
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgspproc.c -shared $(LFLAGS) $(THREADLIBS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgssockets$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgssockets.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgssockets.c -shared $(LFLAGS) $(SOCKLIBS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsmeta$(LIBEXT): $(OUTFILE) $(EXTDIR)/sgsmeta.c
	$(CC) -DSGS_COMPILE_MODULE -o $@ $(EXTDIR)/sgsmeta.c -shared $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)

# the tools
$(OUTDIR)/sgstest$(BINEXT): $(OUTFILE) $(EXTDIR)/sgstest.c
	$(CC) -o $@ $(EXTDIR)/sgstest.c $(LFLAGS) -lm $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OUTDIR)/sgsapitest$(BINEXT): $(OUTFILE) $(EXTDIR)/sgsapitest.c
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
build_test: $(OUTDIR)/sgstest$(BINEXT) json xgmath
build_apitest: $(OUTDIR)/sgsapitest$(BINEXT)
vm: $(OUTDIR)/sgsvm$(BINEXT)
c: $(OUTDIR)/sgsc$(BINEXT)

# test tool aliases
test: build_test
	$(OUTDIR)/sgstest
apitest: build_apitest
	$(OUTDIR)/sgsapitest

.PHONY: tools
tools: xgmath json pproc sockets meta build_test build_apitest vm c

# other stuff
# - cppbc testing
.PHONY: cppbctest
.PHONY: build_cppbctest
build_cppbctest: $(OUTDIR)/sgscppbctest$(BINEXT)
cppbctest: build_cppbctest
	$(OUTDIR)/sgscppbctest
$(OUTDIR)/sgscppbctest$(BINEXT): $(OUTFILE) ext/sgscppbctest.cpp ext/sgscppbctest.h $(OBJDIR)/cppbc_test.cpp ext/cppbc/sgs_cppbc.h
	$(CXX) -o $@ ext/sgscppbctest.cpp $(OBJDIR)/cppbc_test.cpp $(LFLAGS) -lm $(PLATFLAGS) -I. -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
$(OBJDIR)/cppbc_test.cpp: ext/sgscppbctest.h
	bin/sgsvm -p ext/cppbc/cppbc.sgs ext/sgscppbctest.h $@
# - cppbind testing
.PHONY: cppbindtest
.PHONY: build_cppbindtest
build_cppbindtest: $(OUTDIR)/sgscppbindtest$(BINEXT)
cppbindtest: build_cppbindtest
	$(OUTDIR)/sgscppbindtest
$(OUTDIR)/sgscppbindtest$(BINEXT): $(OUTFILE)
	$(CXX) -o $@ ext/cpp/cppbind_example.cpp $(LFLAGS) -lm $(PLATFLAGS) -I. -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
# - multithreaded testing
.PHONY: mttest
.PHONY: build_mttest
build_mttest: $(OUTDIR)/sgstest_mt$(BINEXT)
mttest: build_mttest
	$(OUTDIR)/sgstest_mt
$(OUTDIR)/sgstest_mt$(BINEXT): $(OUTFILE)
	$(CC) -o $@ examples/sgstest_mt.c $(LFLAGS) -lm $(THREADLIBS) $(PLATFLAGS) -I$(SRCDIR) -L$(LIBDIR) $(CFLAGS)
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

endif
