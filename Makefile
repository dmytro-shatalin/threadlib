.PHONY: all prepare build example

all: prepare build

prepare:
	@rm -rf build
	@mkdir build

build:
	@cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@cmake --build build

example: prepare
	@cmake -DTHREADLIB_BUILD_EXAMPLE=ON -S . -B build
	@cmake --build build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	./build/bin/threadlib_example
