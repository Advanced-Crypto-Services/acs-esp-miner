# Flash Firmware

Build and flash firmware to target.

```bash
echo "=== Building ==="
if [ -f "Makefile" ]; then
  make -j$(nproc)
elif [ -d "build" ]; then
  cmake --build build
fi

echo -e "\n=== Flashing ==="
# Detect flash tool
if [ -f "Makefile" ] && grep -q "flash" Makefile; then
  make flash
elif command -v st-flash &> /dev/null; then
  ELF=$(find . -name "*.bin" | head -1)
  echo "Would flash: $ELF"
  echo "Run: st-flash write $ELF 0x8000000"
elif command -v openocd &> /dev/null; then
  echo "OpenOCD available - check your openocd.cfg"
else
  echo "No flash tool detected (st-flash, openocd, J-Link)"
fi
```

Report build status and flash result.
