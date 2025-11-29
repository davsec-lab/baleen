#!/bin/sh

# cat << EOF > .clangd
# CompileFlags:
#   Add: 
#     - -I$HOME/pin/source/include/pin
#     - -I$HOME/pin/source/include/pin/gen
#     - -isystem$HOME/pin/extras/cxx/include
#     - -isystem$HOME/pin/extras/crt/include
#     - -isystem$HOME/pin/extras/crt/include/arch-x86_64
#     - -isystem$HOME/pin/extras/crt/include/kernel/uapi
#     - -isystem$HOME/pin/extras/crt/include/kernel/uapi/asm-x86
#     - -I$HOME/pin/extras/components/include
#     - -I$HOME/pin/extras/xed-intel64/include/xed
#     - -I$HOME/pin/source/tools/Utils
#     - -I$HOME/pin/source/tools/InstLib
#     - -DTARGET_IA32E
#     - -DHOST_IA32E
#     - -DTARGET_LINUX
#     - -DPIN_CRT=1
# EOF

mkdir -p obj-intel64
make obj-intel64/baleen.so TARGET=intel64

# echo "Build complete!"
# echo "Add this to your ~/.bashrc:"
# echo "export PATH=\$PATH:/pin"
# echo "export BALEEN=$(pwd)/obj-intel64/baleen.so"
# echo "alias baleen='pin -t \$BALEEN --'"