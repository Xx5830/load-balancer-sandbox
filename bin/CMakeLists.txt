add_executable(main main.cpp)

target_link_libraries(main PRIVATE library)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:main>
        "${CMAKE_SOURCE_DIR}/build/$<CONFIG>_main"
    COMMENT "Copying $<CONFIG>_main to build/"
)