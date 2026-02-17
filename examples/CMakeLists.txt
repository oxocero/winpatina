# WinPatina Examples

# Visual demo - end-to-end pipeline test
add_executable(visual_demo visual_demo.cpp)
target_link_libraries(visual_demo PRIVATE winpatina_lib)

# Terminal size test - standalone (no winpatina dependency)
add_executable(size_test size_test.cpp)
target_link_libraries(size_test PRIVATE kernel32)

# Windows 2000-friendly smoke test (basic VT output + keyboard read)
add_executable(win2k_smoke win2k_smoke.cpp)
target_link_libraries(win2k_smoke PRIVATE kernel32)
