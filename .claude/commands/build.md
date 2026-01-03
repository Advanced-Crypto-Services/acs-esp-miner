# Build Firmware

Build the firmware project.

```bash
echo "=== Build Environment ==="
if command -v arm-none-eabi-gcc &> /dev/null; then
  arm-none-eabi-gcc --version | head -1
fi

echo -e "\n=== Building ==="
if [ -f "Makefile" ]; then
  make -j$(nproc) 2>&1
elif [ -d "build" ] || [ -f "CMakeLists.txt" ]; then
  cmake --build build 2>&1
else
  echo "No recognized build system found"
fi

echo -e "\n=== Binary Size ==="
find . -name "*.elf" -o -name "*.bin" -o -name "*.hex" 2>/dev/null | head -5 | while read f; do
  ls -lh "$f"
done
```

Report:
1. Build success/failure
2. Warnings (especially -Wall -Wextra violations)
3. Binary size (flash/RAM usage if available)
