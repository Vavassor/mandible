# mandible

A technological adventure-horror game in 2D.

It is just beginning development as of January 2016. Running it right now would show there's nothing more than a basic engine test for audio, visuals, and input.

## Platforms

Only Linux is available right now, as that's what I'm developing using. It will be availble on Windows, Linux, and Raspberry Pi on release of the finished game.

## Building

This source code is all C/C++ and is intended to be built using [CMake](https://cmake.org/download/). If that's not desired or possible, it should be straightforward to compile the files and link with the following libraries on linux: X11 GL udev pthread asound

## Licensing

This software is released into the public domain, with the sole exception of [snes_ntsc](http://slack.net/~ant/libs/ntsc.html), which is under [LGPLv2.1](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html) by Shay Green according to the description in those files. This is temporary, as the intent is for this code to eventually be fully public domain. Also note: [stb_image and stb_vorbis](https://github.com/nothings/stb) are included but are themselves public domain.
