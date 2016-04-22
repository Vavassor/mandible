# mandible

A technological adventure-horror game in 2D.

It is just beginning development as of January 2016. Running it right now would show there's nothing more than a basic engine test for audio, visuals, and input.

## Platforms

Only Linux is available right now, as that's what I'm developing using. It will be availble on Windows, Linux, and Raspberry Pi on release of the finished game.

## Building

This source code is all C/C++ and is intended to be built using [CMake](https://cmake.org/download/). If that's not desired or possible, it should be straightforward to compile the files and link with the following libraries on linux: X11 GL udev pthread asound

## Licensing

This software is released into the public domain according to [the Creative Commons Zero 1.0 public domain dedication](https://creativecommons.org/publicdomain/zero/1.0/). Also note: [stb_image and stb_vorbis](https://github.com/nothings/stb) are included but are themselves public domain.
