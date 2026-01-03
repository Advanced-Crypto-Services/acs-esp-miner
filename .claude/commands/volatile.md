# Volatile Usage Check

Check for potential missing volatile qualifiers.

```bash
echo "=== ISR-shared variables without volatile ==="
# Find variables used in ISRs
grep -rn "_IRQHandler\|_Handler" --include="*.c" -A 20 . 2>/dev/null | grep -E "^\s+\w+\s*=" | head -20

echo -e "\n=== Global variables in headers ==="
grep -rn "^extern" --include="*.h" . 2>/dev/null | grep -v "volatile" | head -20

echo -e "\n=== Register access patterns ==="
grep -rn "0x[0-9A-Fa-f]\{8\}" --include="*.c" --include="*.h" . 2>/dev/null | grep -v volatile | head -10
```

Identify:
1. Variables shared between ISR and main code missing volatile
2. Hardware register accesses that should be volatile
3. DMA buffer declarations
