cmake_minimum_required(VERSION 3.16)
project(cpp_audio_test_answers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(q1_arithmetic_mean   src/q1_arithmetic_mean.cpp)
add_executable(q2_parse_method_name src/q2_parse_method_name.cpp)
add_executable(q6_true_peak         src/q6_true_peak.cpp)

find_package(Threads REQUIRED)
add_executable(q7_audio_engine      src/q7_audio_engine.cpp)
target_link_libraries(q7_audio_engine PRIVATE Threads::Threads)
