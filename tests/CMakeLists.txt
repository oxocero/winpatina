# WinPatina Test Suite

# VT Parser tests
add_executable(test_vt_parser test_vt_parser.cpp)
target_link_libraries(test_vt_parser PRIVATE winpatina_lib)
target_include_directories(test_vt_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME vt_parser COMMAND test_vt_parser)

# Screen Buffer tests
add_executable(test_screen test_screen.cpp)
target_link_libraries(test_screen PRIVATE winpatina_lib)
target_include_directories(test_screen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME screen COMMAND test_screen)

# Colour Mapper tests
add_executable(test_colour test_colour.cpp)
target_link_libraries(test_colour PRIVATE winpatina_lib)
target_include_directories(test_colour PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME colour COMMAND test_colour)

# Dispatch Handler tests
add_executable(test_dispatch test_dispatch.cpp)
target_link_libraries(test_dispatch PRIVATE winpatina_lib)
target_include_directories(test_dispatch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME dispatch COMMAND test_dispatch)
