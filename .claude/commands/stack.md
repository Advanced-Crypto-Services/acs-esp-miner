# Stack Usage Analysis

Analyze stack usage across the firmware.

```bash
echo "=== Rebuilding with -fstack-usage ==="
if [ -f "Makefile" ]; then
  # Check if CFLAGS can accept stack-usage
  make clean 2>/dev/null
  CFLAGS="-fstack-usage" make -j$(nproc) 2>/dev/null || make -j$(nproc)
fi

echo -e "\n=== Stack Usage Files ==="
find . -name "*.su" 2>/dev/null | head -5

echo -e "\n=== Top Stack Consumers ==="
find . -name "*.su" -exec cat {} + 2>/dev/null | sort -t: -k3 -n -r | head -20

echo -e "\n=== Function Call Depth Candidates ==="
grep -rn "recursive\|_Handler\|_IRQHandler" --include="*.c" . 2>/dev/null | head -10
```

Analyze and report:
1. Top stack-consuming functions
2. ISR stack usage
3. Potential stack overflow risks
4. Recommendations for stack sizes
