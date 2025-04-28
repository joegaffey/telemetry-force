# Telemetry Force
Converts telemetry data from games to FFB signals.

## Summary:
✅ Connects to a USB force feedback steering wheel via SDL2  
✅ Receives telemetry from the game: OutSim in Live for Speed to start  
✅ Parses UDP packets into position, heading, velocity, wheel speeds  
✅ Forwards the real steering input into vJoy (virtual joystick)  
✅ Acts as a real-time controller proxy running at ~5ms update rate  

## Build instructions:
Install SDL2 development libraries

Install vJoy SDK headers and library

Compile and link with:

* SDL2.lib
* SDL2main.lib
* ws2_32.lib
* vJoyInterface.lib

Build command (g++):

`g++ lfs_ffb_proxy.cpp -o lfs_ffb_proxy.exe -lSDL2 -lSDL2main -lws2_32 -lvJoyInterface`

(Make sure SDL2.dll and vJoyInterface.dll are next to your .exe.)

## Windows Environment Setup

Install MSYS2 - Gives you GCC (g++) + make + SDL2 + all you need.

Install packages inside MSYS2 shell:

`pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_net mingw-w64-x86_64-SDL2_mixer mingw-w64-x86_64-SDL2_ttf`

Make sure SDL2.dll is next to your compiled exe or inside your PATH.

You can find SDL2.dll from wherever MSYS2 installed SDL2 (or download from libsdl.org).
