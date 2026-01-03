# Fault Analysis

Analyze a HardFault or other exception.

Input (PC, LR, or description): $ARGUMENTS

```bash
echo "=== Searching for fault handlers ==="
grep -rn "HardFault\|MemManage\|BusFault\|UsageFault" --include="*.c" --include="*.h" . 2>/dev/null | head -10

echo -e "\n=== Looking for address in map file ==="
MAP=$(find . -name "*.map" | head -1)
if [ -n "$MAP" ] && [ -n "$ARGUMENTS" ]; then
  grep -i "$ARGUMENTS" "$MAP" 2>/dev/null | head -5
fi

echo -e "\n=== Disassembly around fault (if addr2line available) ==="
ELF=$(find . -name "*.elf" | head -1)
if [ -n "$ELF" ] && [ -n "$ARGUMENTS" ]; then
  arm-none-eabi-addr2line -e "$ELF" "$ARGUMENTS" 2>/dev/null
fi
```

Help diagnose:
1. Fault type (HardFault, MemManage, BusFault, UsageFault)
2. Faulting instruction/function
3. Likely cause
4. Suggested fix
