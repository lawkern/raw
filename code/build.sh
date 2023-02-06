#!/usr/bin/env bash

DEVELOPMENT_BUILD=1

COMPILER_FLAGS="-g -O0 -fdiagnostics-absolute-paths"
COMPILER_FLAGS="${COMPILER_FLAGS} -Werror"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wall"
COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-missing-braces"


if [[ "$DEVELOPMENT_BUILD" == 1 ]]
then
    COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-variable"
    COMPILER_FLAGS="${COMPILER_FLAGS} -Wno-unused-function"
fi

LINKER_FLAGS="-lX11 -lGL -lm"

mkdir -p ../build
pushd ../build > /dev/null

clang ../code/platform_linux.c $COMPILER_FLAGS -o raw $LINKER_FLAGS

popd > /dev/null
