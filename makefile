# SGScript makefile


# Additional compilation info:
# CC (compiler) can be specified in the `make` command
# For Android, together with some android-*** target, pass this:
# CC="<some-prebuilt-gcc/clang> --sysroot=NDK_ROOT\platforms\<platform>\<arch>"


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
CFLAGS=-fwrapv -Wall -Wconversion -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align \
	$(call fnIF_RELEASE,-O3,-D_DEBUG -g) $(call fnIF_COMPILER gcc,-static-libgcc,) \
	$(call fnIF_ARCH,x86,-m32,$(call fnIF_ARCH,x64,-m64,)) -Isrc \
	$(call fnIF_OS,windows,,-fPIC -D_FILE_OFFSET_BITS=64) \
	$(call fnIF_OS,android,-DSGS_PF_ANDROID,)
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
$(OUTDIR)/$(LIBPFX)sgscript$(LIBEXT): $(OBJ)
	$(CC) $(PREFLAGS) -o $@ $(OBJ) $(CFLAGS) -shared -lm
obj/%.o: src/%.c $(DEPS)
	$(CC) $(PREFLAGS) -c -o $@ $< $(COMFLAGS)

## the libraries
$(OUTDIR)/sgsxgmath$(LIBEXT): ext/sgsxgmath.c ext/sgsxgmath.h $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgsjson$(LIBEXT): ext/sgsjson.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgspproc$(LIBEXT): ext/sgspproc.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(THREADLIBS)
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgssockets$(LIBEXT): ext/sgssockets.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS) $(call fnIF_OS,windows,-lws2_32,)
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgsmeta$(LIBEXT): ext/sgsmeta.c $(OUTFILE)
	$(CC) -o $@ $< $(MODULEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)

## the tools
$(OUTDIR)/sgstest$(BINEXT): ext/sgstest.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgsapitest$(BINEXT): ext/sgsapitest.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgsvm$(BINEXT): ext/sgsvm.c ext/sgs_idbg.c ext/sgs_prof.c $(OUTFILE)
	$(CC) -o $@ $(subst $(OUTFILE),,$^) $(EXEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)
$(OUTDIR)/sgsc$(BINEXT): ext/sgsc.c $(OUTFILE)
	$(CC) -o $@ $< $(EXEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)

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
	$(OUTDIR)/sgstest --dir tests
apitest: build_apitest
	$(OUTDIR)/sgsapitest

.PHONY: tools
tools: xgmath json pproc sockets meta build_test build_apitest vm c

## other stuff
## - cppbc testing
.PHONY: cppbctest
.PHONY: build_cppbctest
build_cppbctest: $(OUTDIR)/sgscppbctest$(BINEXT)
cppbctest: build_cppbctest
	$(OUTDIR)/sgscppbctest
$(OUTDIR)/sgscppbctest$(BINEXT): ext/sgscppbctest.cpp obj/cppbc_test.cpp ext/sgscppbctest.h ext/cppbc/sgs_cppbc.h $(OUTFILE)
	$(CXX) -o $@ $< $(word 2,$^) $(EXEFLAGS) -I.
	$(call SGS_INSTALL_TOOL,$@)
obj/cppbc_test.cpp: ext/sgscppbctest.h
	$(OUTDIR)/sgsvm -p ext/cppbc/cppbc.sgs $< $@
## - cppbind testing
.PHONY: cppbindtest
.PHONY: build_cppbindtest
build_cppbindtest: $(OUTDIR)/sgscppbindtest$(BINEXT)
cppbindtest: build_cppbindtest
	$(OUTDIR)/sgscppbindtest
$(OUTDIR)/sgscppbindtest$(BINEXT): ext/cpp/cppbind_example.cpp $(OUTFILE)
	$(CXX) -o $@ $< $(EXEFLAGS)
	$(call SGS_INSTALL_TOOL,$@)
## - multithreaded testing
.PHONY: mttest
.PHONY: build_mttest
build_mttest: $(OUTDIR)/sgstest_mt$(BINEXT)
mttest: build_mttest
	$(OUTDIR)/sgstest_mt
$(OUTDIR)/sgstest_mt$(BINEXT): examples/sgstest_mt.c $(OUTFILE)
	$(CC) -o $@ $^ $(EXEFLAGS) $(THREADLIBS)
	$(call SGS_INSTALL_TOOL,$@)
## - sgs2exe tool
.PHONY: sgsexe
sgsexe: ext/sgsexe.c $(OUTFILE_STATIC)
	$(CC) -o $(OUTDIR)/sgsexe.exe $^ $(CFLAGS) -lm
	copy /B $(OUTDIR)\sgsexe.exe + ext\stubapp.bin $(OUTDIR)\sgsexe.tmp
	del /Q $(OUTDIR)\sgsexe.exe
	cmd /c move /Y $(OUTDIR)\sgsexe.tmp $(OUTDIR)\sgsexe.exe
## - binary archive preparation
.PHONY: binarch
binarch: vm
	sgsvm build/prep.sgs

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

