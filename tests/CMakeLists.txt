# WinPatina Test Suite

# VT Parser tests
add_executable(test_vt_parser test_vt_parser.cpp)
target_link_libraries(test_vt_parser PRIVATE winpatina_lib)
target_include_directories(test_vt_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME vt_parser COMMAND test_vt_parser)
