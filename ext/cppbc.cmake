
set( SGSVM_PATH "sgsvm" CACHE FILEPATH
	"Path to SGSVM (SGScript virtual machine)" )
set( SGSCPPBC_PATH "../ext/cppbc.sgs" CACHE FILEPATH
	"Path to cppbc.sgs (SGS/CPP-BC binding code generator)" )

if( MSVC )
	set( CPPBC_PP_ARGS /P /DSGS_CPPBC_PP=1 /Fi )
else()
	set( CPPBC_PP_ARGS -E -DSGS_CPPBC_PP=1 -o )
endif()

macro( cppbc_header CPPFILE HFILE INCNAME )
	set(list ${CMAKE_CXX_FLAGS})
	separate_arguments(list)
	message(${list})
	add_custom_command(
		OUTPUT "${CMAKE_SOURCE_DIR}/${CPPFILE}"
		DEPENDS "${HFILE}"
		COMMAND ${CMAKE_CXX_COMPILER} ARGS ${CPPBC_PP_ARGS} cppbc_tmp.i "${CMAKE_SOURCE_DIR}/${HFILE}" ${list}
		COMMAND ${SGSVM_PATH} ARGS -p ${SGSCPPBC_PATH} -o "${CMAKE_SOURCE_DIR}/${CPPFILE}" -iname ${INCNAME} cppbc_tmp.i
	)
endmacro( cppbc_header )

macro( cppbc_headers HFILES )
	foreach( FILE ${HFILES} )
		get_filename_component( FDIR ${FILE} PATH )
		get_filename_component( FNAME ${FILE} NAME_WE )
		get_filename_component( FXNAME ${FILE} NAME )
		set( OUTFILE "${FDIR}/cppbc_${FNAME}.cpp" )
		cppbc_header( ${OUTFILE} ${FILE} ${FXNAME} )
	endforeach( FILE )
endmacro( cppbc_headers )
