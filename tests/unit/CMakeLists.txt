# UNIT TESTS

IF(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
ENDIF(MSVC)

include_directories(
  ${GROK_BINARY_DIR}/src/lib/jp2 # grk_config.h
  ${GROK_SOURCE_DIR}/src/lib/jp2
)

set(unit_test
  testempty0
  testempty1
  testempty2
)
foreach(ut ${unit_test})
  add_executable(${ut} ${ut}.cpp)
  target_link_libraries(${ut} ${GROK_LIBRARY_NAME} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
  add_test(NAME ${ut} COMMAND ${ut})
endforeach()
