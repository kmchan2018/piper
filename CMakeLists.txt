
cmake_minimum_required (VERSION 3.0)
cmake_minimum_required (VERSION 3.0 FATAL_ERROR)

project (piper CXX)

set (PIPER_VERSION_MAJOR 0)
set (PIPER_VERSION_MINOR 0)
set (PIPER_VERSION_PATCH 1)

set (
	CORE_SOURCES
	alsa.cpp
	file.cpp
	device.cpp
	operation.cpp
	pipe.cpp
	signpost.cpp
	timer.cpp
	tokenbucket.cpp
	transport.cpp
)

configure_file (
  "${PROJECT_SOURCE_DIR}/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
)

include_directories ("${PROJECT_BINARY_DIR}")

include (GNUInstallDirs)
add_compile_options (-std=c++11 -Wall -Wextra -pedantic -pedantic-errors -Wabi -Wcast-align -Wcast-qual -Wconversion -Wctor-dtor-privacy -Wdisabled-optimization -Wfloat-equal -Wformat=2 -Wformat-signedness -Winit-self -Winvalid-pch -Wlogical-op -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn -Wnoexcept -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wregister -Wredundant-decls -Wshadow -Wsign-conversion -Wsign-promo -Wstrict-null-sentinel -Wstrict-overflow=5 -Wswitch-default -Wswitch-enum -Wundef -Wunsafe-loop-optimizations -Wunused -Wuseless-cast -Wzero-as-null-pointer-constant -Werror)
add_definitions (-DPIC)

add_library (core OBJECT ${CORE_SOURCES})
set_target_properties (core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable (piper main.cpp)
add_dependencies (piper core)

add_library (asound_module_pcm_piper SHARED plugin.cpp)
add_dependencies (asound_module_pcm_piper core)
set_target_properties (asound_module_pcm_piper PROPERTIES LINK_FLAGS -Wl,--version-script=libasound_module_pcm_piper.version)

target_link_libraries (piper asound $<TARGET_OBJECTS:core>)
target_link_libraries (asound_module_pcm_piper asound $<TARGET_OBJECTS:core>)

install (TARGETS piper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install (TARGETS asound_module_pcm_piper LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/alsa PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)


