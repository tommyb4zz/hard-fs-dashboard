#!/bin/sh
# Purpose: fetch a pinned FreeRTOS kernel into build/_deps for machines without the ST FreeRTOS
# pack, and report whether an arm-none-eabi toolchain can be found. CMakeLists.txt and
# cmake/gnu-tools-for-stm32.cmake look in build/_deps automatically; nothing is installed
# system-wide. The ST pack, when installed, is still preferred by the firmware build.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
deps="$root/build/_deps"
kernel_tag="V10.6.2"
mkdir -p "$deps"

if [ -f "$deps/FreeRTOS-Kernel/tasks.c" ]; then
  echo "FreeRTOS kernel present: $deps/FreeRTOS-Kernel"
else
  git clone --depth 1 --branch "$kernel_tag" https://github.com/FreeRTOS/FreeRTOS-Kernel.git \
    "$deps/FreeRTOS-Kernel"
  echo "FreeRTOS kernel $kernel_tag fetched into $deps/FreeRTOS-Kernel"
fi

if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "ARM toolchain on PATH: $(command -v arm-none-eabi-gcc)"
elif [ -x "$deps/arm-toolchain-expanded/Payload/bin/arm-none-eabi-gcc" ]; then
  echo "ARM toolchain found: $deps/arm-toolchain-expanded/Payload/bin"
elif ls /Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "ARM toolchain found under /Applications/ArmGNUToolchain"
else
  echo "No arm-none-eabi toolchain found. Install the STM32 VS Code extension bundle or the" >&2
  echo "Arm GNU Toolchain (arm-none-eabi) from developer.arm.com, then configure again." >&2
  exit 1
fi
