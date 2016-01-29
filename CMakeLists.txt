cmake_minimum_required (VERSION 2.8)
project (Mandible)

# ----- INCLUDE FILES -----
set (INCLUDES
	atomic.h
	audio.h
	evdev_text.h
	input.h
	logging.h
	sized_types.h
	snes_ntsc.h
	snes_ntsc_config.h
	snes_ntsc_impl.h
	stb_image.h
	wave_decoder.h
)
source_group ("Header Files" FILES ${INCLUDES})

# ----- SOURCE FILES -----
set (SOURCES
	atomic.c
	audio.cpp
	evdev_text.cpp
	input.cpp
	logging.cpp
	main.cpp
	snes_ntsc.c
	stb_vorbis.c
	wave_decoder.c
)
source_group ("Source Files" FILES ${SOURCES})

add_executable (Mandible ${SOURCES} ${INCLUDES})

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

set_target_properties (Mandible PROPERTIES COMPILE_DEFINITIONS "${DEFINITIONS}")
set_target_properties (Mandible PROPERTIES COMPILE_DEFINITIONS_DEBUG "${DEBUG_DEFINITIONS}")
set_target_properties (Mandible PROPERTIES COMPILE_DEFINITIONS_RELEASE "${RELEASE_DEFINITIONS}")
set_target_properties (Mandible PROPERTIES COMPILE_DEFINITIONS_RELWITHDEBINFO ${RELEASE_DEFINITIONS})
set_target_properties (Mandible PROPERTIES COMPILE_DEFINITIONS_MINSIZEREL ${RELEASE_DEFINITIONS})

# ----- LIBRARIES -----
set (LIBRARIES
	xcb
	xcb-icccm
	xcb-keysyms
	xcb-shm
	udev
	pthread
	asound)
target_link_libraries (Mandible ${LIBRARIES})
