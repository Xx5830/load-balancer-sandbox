file(GLOB ALL_CPP CONFIGURE_DEPENDS "*.hpp")

add_library(library ${ALL_CPP})

target_include_directories(library PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(library PROPERTIES LINKER_LANGUAGE CXX)