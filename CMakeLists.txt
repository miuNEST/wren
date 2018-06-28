macro(use_c99)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "-std=c99 ${CMAKE_C_FLAGS}")
    endif ()
  else ()
	if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    	set (CMAKE_C_STANDARD 99)
	endif ()
  endif ()
endmacro(use_c99)

use_c99()

aux_source_directory(./src/vm SRC_VM)
aux_source_directory(./src/include SRC_INCLUDE)
aux_source_directory(./src/contract SRC_CONTRACT)
aux_source_directory(./src/optional SRC_OPTIONAL)
aux_source_directory(./src/cjson SRC_CJSON)

add_library( graphene_wren
	${SRC_VM}
	${SRC_INCLUDE}
	${SRC_CONTRACT}
	${SRC_OPTIONAL}
	${SRC_CJSON}
)

target_link_libraries( graphene_wren graphene_chain fc )

target_include_directories( graphene_wren
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/vm"
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/optional"
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/cjson"
)