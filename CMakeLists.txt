# file(GLOB headers "include/graphene/wren/*.hpp" "include/graphene/wren/*.h")

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