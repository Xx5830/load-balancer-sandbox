file(GLOB ALL_HPP CONFIGURE_DEPENDS "*.hpp")

add_library(library ${ALL_HPP})

target_link_libraries(library PUBLIC asio nlohmann_json)

target_include_directories(library PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(library PROPERTIES LINKER_LANGUAGE CXX)