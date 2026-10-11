cmake_minimum_required(VERSION 3.16)
project(companion_goals_tests LANGUAGES CXX)
enable_testing()
add_executable(goals_test goals_test.cpp)
target_compile_features(goals_test PRIVATE cxx_std_20)
target_include_directories(goals_test PRIVATE ../../src/gameplay/Bot ../../src/narrative/character
    ../../src/narrative/llm ../../deps)
add_test(NAME companion_goal_lifecycle COMMAND goals_test "${CMAKE_CURRENT_BINARY_DIR}/goals-history.json")
