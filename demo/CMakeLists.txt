set(3RD_PATH "${PROJECT_SOURCE_DIR}/3rd")

file(GLOB SRC "*.cpp" "*.hpp" "*.h" "*.c")
set(EXE_NAME netdemo)
source_group("" FILES ${SRC})
ADD_EXECUTABLE(${EXE_NAME} ${SRC})
target_link_libraries(${EXE_NAME} ${OPENSSL_LIBRARY})


#add_custom_command(
 #   TARGET ${EXE_NAME} POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy
#    ${MOD_SOURCE_PATH}/${EXE_NAME}
#    ${MOD_OUTPUT_PATH}/${EXE_NAME}
#)
#set_target_properties(${EXE_NAME} PROPERTIES 
#	PREFIX ""
#	FOLDER "luamod"
#)