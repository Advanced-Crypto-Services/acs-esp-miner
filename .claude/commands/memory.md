# Memory Map Analysis

Analyze memory usage from linker output.

```bash
echo "=== Map File ==="
MAP=$(find . -name "*.map" | head -1)
if [ -n "$MAP" ]; then
  echo "Found: $MAP"
  echo -e "\n=== Memory Regions ==="
  grep -A 20 "Memory Configuration" "$MAP" 2>/dev/null | head -25
  echo -e "\n=== Section Sizes ==="
  grep -E "^\.(text|data|bss|rodata)" "$MAP" 2>/dev/null | head -20
else
  echo "No .map file found"
fi

echo -e "\n=== Size Command ==="
ELF=$(find . -name "*.elf" | head -1)
if [ -n "$ELF" ]; then
  arm-none-eabi-size "$ELF" 2>/dev/null || size "$ELF" 2>/dev/null
fi
```

Report:
1. Flash usage (text + rodata + data)
2. RAM usage (data + bss)
3. Available headroom
4. Largest symbols
