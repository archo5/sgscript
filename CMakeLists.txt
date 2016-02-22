
cmake_minimum_required( VERSION 2.6 )

project( SGScript )

include( ext/cppbc.cmake )
set( SGSVM_PATH "${CMAKE_SOURCE_DIR}/bin/sgsvm" )
# set( SGSCPPBC_PATH "${CMAKE_SOURCE_DIR}/ext/cppbc.sgs" )

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin" )
set( CMAKE_SHARED_LIBRARY_PREFIX "" )

if(NOT MSVC)
	set(PF_CORE_FLAGS "-m32")
	set(CORE_FLAGS "${PF_CORE_FLAGS} -fwrapv -Wall -Wconversion -Wno-shadow -Wpointer-arith -Wcast-qual -Wcast-align -static-libgcc -Wno-missing-braces -g -fno-unwind-tables -ffunction-sections -I${CMAKE_SOURCE_DIR}/src")
	set(CXX_CORE_FLAGS "${CORE_FLAGS} -fno-rtti -fno-exceptions")
	if(NOT WIN32)
		set(CORE_FLAGS "${CORE_FLAGS} -fPIC")
	endif(NOT WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CORE_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_CORE_FLAGS}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CXX_CORE_FLAGS}")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${CXX_CORE_FLAGS}")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${CXX_CORE_FLAGS}")
else()
	set(CORE_FLAGS "")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR- /W3 /Zi /wd4244 /wd4996")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /DEBUG")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG")
endif()

set( SGSCRIPT_FILES
	src/sgs_bcg.c
	src/sgs_ctx.c
	src/sgs_fnt.c
	src/sgs_proc.c
	src/sgs_regex.c
	src/sgs_std.c
	src/sgs_stdL.c
	src/sgs_tok.c
	src/sgs_util.c
	src/sgs_xpc.c
)
add_library( sgscript SHARED ${SGSCRIPT_FILES} )

# SGSVM
add_executable( sgsvm ext/sgsvm.c ext/sgs_prof.c ext/sgs_idbg.c )
target_include_directories( sgsvm PRIVATE src )
target_link_libraries( sgsvm sgscript )

# SGS/CPP-BC test
add_executable( sgscppbctest ext/sgscppbctest.cpp obj/cppbc_test.cpp )
add_dependencies( sgscppbctest sgsvm )
target_include_directories( sgscppbctest PRIVATE src )
cppbc_header( sgscppbctest obj/cppbc_test.cpp ext/sgscppbctest.h ../ext/sgscppbctest.h )
target_link_libraries( sgscppbctest sgscript )
