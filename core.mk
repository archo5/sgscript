
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
ifeq ($(OUTDIR),)
	OUTDIR=bin
endif


# UTILITIES
cOS=os_unknown
cARCH=arch_unknown
cCOMPILER=compiler_unknown
ifeq ($(OS),Windows_NT)
	fnREMOVE_ALL = del /F /S /Q
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
	UNAME_M := $(shell uname -m)
	ifneq ($(filter %86,$(UNAME_M)),)
		cARCH=x86
	endif
	ifeq ($(UNAME_M),x86_64)
		cARCH=x64
	endif
	ifneq ($(filter arm%,$(UNAME_M)),)
		cARCH=arm
	endif
endif
CC_V := $(shell $(CC) 2>&1)
ifneq ($(findstring clang,$(CC_V)),)
	cCOMPILER=clang
endif
ifneq ($(findstring gcc,$(CC_V)),)
	cCOMPILER=gcc
endif


ifeq ($(os),)
	os=$(cOS)
endif
ifeq ($(arch),)
	arch=$(cARCH)
endif
cIF_RELEASE=$(findstring release,$(mode))
fnIF_RELEASE=$(if $(cIF_RELEASE),$1,$2)
fnIF_OS=$(if $(findstring $1,$(os)),$2,$3)
fnIF_ARCH=$(if $(findstring $1,$(arch)),$2,$3)
fnIF_OS_ARCH=$(if $(findstring $1,$(target)),$2,$3)
fnIF_COMPILER=$(if $(findstring $1,$(cCOMPILER)),$2,$3)


# PLATFORM SPECIFICS
ifeq ($(os),windows)
	BINEXT=.exe
	LIBPFX=
	LIBEXT=.dll
else
	BINEXT=
	LIBPFX=lib
	LIBEXT=.so
endif
