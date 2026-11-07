# Build with the CE C/C++ toolchain (CEdev): just run `make`
NAME = STLFRONT
DESCRIPTION = "Steel Front - tank battle"
COMPRESSED = NO
ARCHIVED = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

include $(shell cedev-config --makefile)
