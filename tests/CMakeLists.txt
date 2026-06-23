include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)

FetchContent_MakeAvailable(googletest)

file(GLOB ALL_TESTS CONFIGURE_DEPENDS "*.cpp")

add_executable(testing ${ALL_TESTS})

target_link_libraries(testing PRIVATE library gtest_main nlohmann_json_schema_validator)

target_compile_definitions(testing PRIVATE SCHEMS_DIR="${CMAKE_SOURCE_DIR}/schems")

include(GoogleTest)
gtest_discover_tests(testing TEST_FILTER -BenchmarkOutputStress.*)
string(RANDOM LENGTH 1 ALPHABET "123456789" STRESS_SPAN_RAW)
math(EXPR STRESS_SPAN "${STRESS_SPAN_RAW} + 4") # [5, 13]
foreach(i RANGE 1 ${STRESS_SPAN})
    string(RANDOM LENGTH 9 ALPHABET "0123456789" STRESS_SEED)
    add_test(NAME "BenchmarkOutputStress.HeavyRun.seed${STRESS_SEED}"
             COMMAND testing --gtest_filter=BenchmarkOutputStress.*)
    set_tests_properties("BenchmarkOutputStress.HeavyRun.seed${STRESS_SEED}"
                         PROPERTIES ENVIRONMENT "STRESS_SEED=${STRESS_SEED}")
endforeach()