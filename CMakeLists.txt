cmake_minimum_required(VERSION 3.24)

project(load_balancing VERSION 1.0.0 LANGUAGES CXX)

if(NOT DEFINED CMAKE_PROJECT_NAME)
    message(FATAL_ERROR "Не вызывайте cmake напрямую. Используйте just (just setup && just build)")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/build/.deps" CACHE PATH "Общая папка для FetchContent")

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

if(nlohmann_json_POPULATED)
    message(STATUS "nlohmann/json: загружен (v3.11.3)")
else()
    message(WARNING "nlohmann/json: не удалось загрузить, используется системная версия?")
endif()

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

if(asio_POPULATED)
    message(STATUS "asio: загружен (asio-1-28-0)")
else()
    message(WARNING "asio: не удалось загрузить, используется системная версия?")
endif()

if(NOT TARGET asio)
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE "${asio_SOURCE_DIR}/asio/include")
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
endif()

if(BUILD_TESTING)
    set(JSON_VALIDATOR_INSTALL OFF CACHE BOOL "" FORCE)
    set(JSON_VALIDATOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        json_schema_validator
        GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
        GIT_TAG 2.3.0
    )
    FetchContent_MakeAvailable(json_schema_validator)
    if(json_schema_validator_POPULATED)
        message(STATUS "json-schema-validator: загружен (2.3.0)")
    endif()
endif()

add_subdirectory(libs)
add_subdirectory(bin)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

file(CREATE_LINK
     "${CMAKE_BINARY_DIR}/compile_commands.json"
     "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
     COPY_ON_ERROR SYMBOLIC
)