#!/bin/sh

mkdir -p obj-intel64
make obj-intel64/baleen.so TARGET=intel64

PIN_DIR=$(dirname $(dirname $(dirname $(pwd))))

echo "\nBuild complete! Add the commands below to your shell configuration file.\n"
echo "export PATH=\$PATH:$PIN_DIR"
echo "export BALEEN=$(pwd)/obj-intel64/baleen.so"
echo "alias baleen='pin -t \$BALEEN --'"