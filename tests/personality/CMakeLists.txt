cmake_minimum_required(VERSION 3.16)
project(pbc_personality_tests LANGUAGES CXX)
enable_testing()
add_executable(personality_test personality_test.cpp)
target_compile_features(personality_test PRIVATE cxx_std_20)
target_include_directories(personality_test PRIVATE ../../src/narrative/character ../../src/narrative/llm ../../deps)
add_test(NAME permanent_personality COMMAND personality_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../knowledge/personality-traits.json")
