cmake_minimum_required (VERSION 2.8)
project (Mandible)

# ----- INCLUDE FILES -----
set (INCLUDES
    ani_file.h
    atomic.h
    audio.h
    draw.h
    evdev_text.h
    font.h
    game.h
    gl_core_3_3.h
    gl_shader.h
    glx_extensions.h
    input.h
    logging.h
    memory.h
    monitoring.h
    posix_errors.h
    random.h
    sized_types.h
    stb_image.h
    string_utilities.h
    unicode.h
    wave_decoder.h
)
source_group ("Header Files" FILES ${INCLUDES})

# ----- SOURCE FILES -----
set (SOURCES
    ani_file.cpp
    atomic.cpp
    audio.cpp
    draw.cpp
    evdev_text.cpp
    font.cpp
    game.cpp
    gl_core_3_3.c
    gl_shader.cpp
    glx_extensions.cpp
    input.cpp
    logging.cpp
    main.cpp
    memory.cpp
    monitoring.cpp
    posix_errors.cpp
    random.cpp
    stb_vorbis.c
    string_utilities.cpp
    unicode.cpp
    wave_decoder.cpp
)
source_group ("Source Files" FILES ${SOURCES})

add_executable (mandible ${SOURCES} ${INCLUDES})

# ----- DEFINES -----
if (CMAKE_COMPILER_IS_GNUCXX)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif ()
if (MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
endif ()

set (DEFINITIONS)
set (DEBUG_DEFINITIONS _DEBUG)
set (RELEASE_DEFINITIONS NDEBUG)

set_target_properties (mandible PROPERTIES COMPILE_DEFINITIONS "${DEFINITIONS}")
set_target_properties (mandible PROPERTIES COMPILE_DEFINITIONS_DEBUG "${DEBUG_DEFINITIONS}")
set_target_properties (mandible PROPERTIES COMPILE_DEFINITIONS_RELEASE "${RELEASE_DEFINITIONS}")
set_target_properties (mandible PROPERTIES COMPILE_DEFINITIONS_RELWITHDEBINFO ${RELEASE_DEFINITIONS})
set_target_properties (mandible PROPERTIES COMPILE_DEFINITIONS_MINSIZEREL ${RELEASE_DEFINITIONS})

# ----- LIBRARIES -----
set (LIBRARIES
    X11
    GL
    udev
    pthread
    asound
)
target_link_libraries (mandible ${LIBRARIES})
