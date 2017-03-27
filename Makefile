# Creation rules for chip8emu
#

CXX=clang++
CXXFLAGS=-g -Wall -Wextra -lSDLShapes -std=c++1z -O2 `sdl2-config --cflags --libs`

chip8emu.exe: chip8emu.cpp
	$(CXX) -o $@ $(CXXFLAGS) $^
	
