.PHONY: all prepare build example

all: prepare build

prepare:
	@rm -rf build
	@mkdir build

build:
	@cmake -S . -B build
	@cmake --build build

example: prepare
	@cmake -DTHREADLIB_BUILD_EXAMPLE=ON -S . -B build
	@cmake --build build
	./build/bin/threadlib_example
