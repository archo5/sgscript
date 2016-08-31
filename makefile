# SGScript makefile


include core.mk


# APP FLAGS
OUTFILE_STATIC=lib/libsgscript.a
OUTFILE_DYNAMIC=$(OUTDIR)/$(LIBPFX)sgscript$(LIBEXT)
OUTFLAGS_STATIC=-Llib -lsgscript
OUTFLAGS_DYNAMIC=-L$(OUTDIR) -lsgscript
ifneq ($(static),)
	PREFLAGS=-DBUILDING_SGS=1
	OUTFILE=$(OUTFILE_STATIC)
	OUTFLAGS=$(OUTFLAGS_STATIC)
else
	PREFLAGS=-DBUILDING_SGS=1 -DSGS_DLL=1
	OUTFILE=$(OUTFILE_DYNAMIC)
	OUTFLAGS=$(OUTFLAGS_DYNAMIC)
endif
CFLAGS=-Wall -Wconversion -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align \
	$(call fnIF_RELEASE,-O2,-D_DEBUG -g) $(call fnIF_COMPILER,gcc,-static-libgcc,) \
	$(call fnIF_ARCH,x86,-m32,$(call fnIF_ARCH,x64,-m64,)) -Isrc \
	$(call fnIF_OS,windows,,-fPIC -D_FILE_OFFSET_BITS=64) \
	$(call fnIF_OS,android,-DSGS_PF_ANDROID,) $(XCFLAGS)
COMFLAGS=$(CFLAGS)
BINFLAGS=$(CFLAGS) $(OUTFLAGS) -lm \
	$(call fnIF_OS,android,-ldl -Wl$(comma)-rpath$(comma)'$$ORIGIN' -Wl$(comma)-z$(comma)origin,) \
	$(call fnIF_OS,windows,-lkernel32,) \
	$(call fnIF_OS,osx,-ldl -Wl$(comma)-rpath$(comma)'@executable_path/.',) \
	$(call fnIF_OS,linux,-ldl -lrt -Wl$(comma)-rpath$(comma)'$$ORIGIN' -Wl$(comma)-z$(comma)origin,)
MODULEFLAGS=-DSGS_COMPILE_MODULE $(BINFLAGS) -shared
EXEFLAGS=$(BINFLAGS)
THREADLIBS=$(call fnIF_OS,windows,,$(call fnIF_OS,android,,-lpthread))
SGS_INSTALL_TOOL = $(call fnIF_OS,osx,install_name_tool \
	-change $(OUTDIR)/libsgscript.so @rpath/libsgscript.so $1,)


# BUILD INFO
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),clean_obj)
ifneq ($(MAKECMDGOALS),clean_objbin)
$(info -------------------)
$(info SGScript build info)
$(info -------------------)
$(info PLATFORM - $(cOS)/$(cARCH))
$(info COMPILER - $(cCOMPILER))
$(info TARGET - $(os)/$(arch))
$(info MODE - $(call fnIF_RELEASE,Release,Debug))
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

DEPS = $(patsubst %,src/%.h,sgs_int sgs_regex sgs_util sgscript)
OBJ = $(patsubst %,obj/sgs_%.o,bcg ctx fnt proc regex srlz std stdL tok util xpc)

lib/libsgscript.a: $(OBJ)
	ar rcs $@ $(OBJ)
$(OUTDIR)/$(LIBPFX)sgscript$(LIBEXT): $(OBJ)
	$(CC) $(PREFLAGS) -o $@ $(OBJ) $(CFLAGS) -shared -lm $(call fnIF_OS,osx,-dynamiclib -install_name @rpath/$(LIBPFX)sgscript$(LIBEXT),)
obj/%.o: src/%.c $(DEPS)
	$(CC) $(PREFLAGS) -c -o $@ $< $(COMFLAGS)

## the libraries
$(OUTDIR)/$(LIBPFX)sgsxgmath$(LIBEXT): ext/sgsxgmath.c ext/sgsxgmath.h $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(call fnIF_OS,osx,-dynamiclib -install_name @rpath/$(LIBPFX)sgsxgmath$(LIBEXT),)
$(OUTDIR)/$(LIBPFX)sgsjson$(LIBEXT): ext/sgsjson.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(call fnIF_OS,osx,-dynamiclib -install_name @rpath/$(LIBPFX)sgsjson$(LIBEXT),)
$(OUTDIR)/$(LIBPFX)sgspproc$(LIBEXT): ext/sgspproc.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(THREADLIBS) $(call fnIF_OS,osx,-dynamiclib -install_name @rpath/$(LIBPFX)sgspproc$(LIBEXT),)
$(OUTDIR)/$(LIBPFX)sgssockets$(LIBEXT): ext/sgssockets.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(call fnIF_OS,windows,-lws2_32,) $(call fnIF_OS,osx,-dynamiclib -install_name @rpath/$(LIBPFX)sgssockets$(LIBEXT),)
$(OUTDIR)/$(LIBPFX)sgsmeta$(LIBEXT): ext/sgsmeta.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(call fnIF_OS,osx,-dynamiclib -install_name @rpath/$(LIBPFX)sgsmeta$(LIBEXT),)

## the tools
$(OUTDIR)/sgstest$(BINEXT): ext/sgstest.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)
$(OUTDIR)/sgsapitest$(BINEXT): ext/sgsapitest.c ext/sgs_prof.c ext/sgsapitest_core.h $(OUTFILE)
	$(CC) -o $@ ext/sgsapitest.c ext/sgs_prof.c $(EXEFLAGS)
$(OUTDIR)/sgsvm$(BINEXT): ext/sgsvm.c ext/sgs_idbg.c ext/sgs_prof.c $(OUTFILE)
	$(CC) -o $@ $(subst $(OUTFILE),,$^) $(EXEFLAGS)
$(OUTDIR)/sgsc$(BINEXT): ext/sgsc.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)

## library/tool aliases
.PHONY: xgmath json pproc sockets meta
.PHONY: build_test build_apitest
.PHONY: vm c test apitest
xgmath: $(OUTDIR)/$(LIBPFX)sgsxgmath$(LIBEXT)
json: $(OUTDIR)/$(LIBPFX)sgsjson$(LIBEXT)
pproc: $(OUTDIR)/$(LIBPFX)sgspproc$(LIBEXT)
sockets: $(OUTDIR)/$(LIBPFX)sgssockets$(LIBEXT)
meta: $(OUTDIR)/$(LIBPFX)sgsmeta$(LIBEXT)
build_test: $(OUTDIR)/sgstest$(BINEXT) json xgmath meta
build_apitest: $(OUTDIR)/sgsapitest$(BINEXT)
build_ubench: $(OUTDIR)/sgsubench$(BINEXT)
vm: $(OUTDIR)/sgsvm$(BINEXT)
c: $(OUTDIR)/sgsc$(BINEXT)

# test tool aliases
test: build_test
	$(OUTDIR)/sgstest --dir tests
apitest: build_apitest
	$(OUTDIR)/sgsapitest

.PHONY: tools
tools: xgmath json pproc sockets meta build_test build_apitest vm c

## other stuff
## - cppbc testing
.PHONY: cppbctest build_cppbctest cppbctest11 build_cppbctest11
build_cppbctest: $(OUTDIR)/sgscppbctest$(BINEXT)
build_cppbctest11: $(OUTDIR)/sgscppbctest11$(BINEXT)
cppbctest: $(OUTDIR)/sgscppbctest$(BINEXT)
	$(OUTDIR)/sgscppbctest
cppbctest11: $(OUTDIR)/sgscppbctest11$(BINEXT)
	$(OUTDIR)/sgscppbctest11
$(OUTDIR)/sgscppbctest$(BINEXT): ext/sgscppbctest.cpp obj/cppbc_test.cpp ext/sgscppbctest.h ext/sgsapitest_core.h ext/sgs_cppbc.h $(OUTFILE)
	$(CXX) -o $@ $< $(word 2,$^) $(EXEFLAGS) -I. -std=c++03 -Wno-shadow
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgscppbctest11$(BINEXT): ext/sgscppbctest.cpp obj/cppbc_test.cpp ext/sgscppbctest.h ext/sgsapitest_core.h ext/sgs_cppbc.h $(OUTFILE)
	$(CXX) -o $@ $< $(word 2,$^) $(EXEFLAGS) -I. -std=c++11 -Wno-shadow
	$(call SGS_INSTALL_TOOL,$@)
obj/cppbc_test.cpp: ext/sgscppbctest.h $(OUTDIR)/sgsvm$(BINEXT)
	$(OUTDIR)/sgsvm -p ext/cppbc.sgs $< -o $@ -iname ../ext/sgscppbctest.h
## - multithreaded testing
.PHONY: mttest build_mttest
build_mttest: $(OUTDIR)/sgstest_mt$(BINEXT)
mttest: $(OUTDIR)/sgstest_mt$(BINEXT)
	$(OUTDIR)/sgstest_mt
$(OUTDIR)/sgstest_mt$(BINEXT): examples/sgstest_mt.c $(OUTFILE)
	$(CC) -o $@ $^ $(EXEFLAGS) $(THREADLIBS)
	$(call SGS_INSTALL_TOOL,$@)
## - instrumented profiling
.PHONY: ubench build_ubench
ubench: $(OUTDIR)/sgsubench$(BINEXT)
	$(OUTDIR)/sgsubench
$(OUTDIR)/sgsubench$(BINEXT): ext/sgsubench.c
	$(CC) -o $@ $^ $(EXEFLAGS) $(COMFLAGS)
## - sgs2exe tool
.PHONY: sgsexe
sgsexe: ext/sgsexe.c $(OUTFILE_STATIC)
	$(CC) -o $(OUTDIR)/sgsexe.exe $^ $(CFLAGS) -lm
	copy /B $(OUTDIR)\sgsexe.exe + ext\stubapp.bin $(OUTDIR)\sgsexe.tmp
	del /Q $(OUTDIR)\sgsexe.exe
	cmd /c move /Y $(OUTDIR)\sgsexe.tmp $(OUTDIR)\sgsexe.exe
## - binary archive preparation
.PHONY: binarch
binarch: clean vm
	$(OUTDIR)/sgsvm build/prep.sgs
## - documentation preparation
.PHONY: docs
docs: vm json
	cd docs && $(call fnFIX_PATH,../$(OUTDIR)/sgsvm) -p docgen -e
## - single header versions
.PHONY: shv
shv: vm
	$(OUTDIR)/sgsvm -p build/hgen -o $(OUTDIR)/sgscript-impl.h
	$(OUTDIR)/sgsvm -p build/hgen -m -o $(OUTDIR)/sgscript-no-stdlib-impl.h

## clean build data
.PHONY: clean_obj
clean_obj:
	-$(fnREMOVE_ALL) $(call fnFIX_PATH,obj/*.o)

.PHONY: clean_objbin
clean_objbin:
	-$(fnREMOVE_ALL) $(call fnFIX_PATH,obj/*.o $(OUTDIR)/sgs* $(OUTDIR)/libsgs*)

.PHONY: clean
clean:
	-$(fnREMOVE_ALL) $(call fnFIX_PATH,obj/*.o lib/*.a lib/*.lib $(OUTDIR)/sgs* $(OUTDIR)/libsgs*)

