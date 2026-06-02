include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG  v1.17.0
)

FetchContent_MakeAvailable(googletest)

file(GLOB ALL_TESTS CONFIGURE_DEPENDS "*.cpp")

add_executable(testing ${ALL_TESTS})

target_link_libraries(testing PRIVATE library gtest_main)

include(GoogleTest)
gtest_discover_tests(testing)