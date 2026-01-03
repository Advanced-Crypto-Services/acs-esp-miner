# Deep Analysis

Perform thorough analysis of: $ARGUMENTS

```bash
TARGET="$ARGUMENTS"
if [ -z "$TARGET" ]; then
  TARGET="."
fi

echo "=== File Structure ==="
find "$TARGET" -type f \( -name "*.c" -o -name "*.h" -o -name "*.ts" -o -name "*.tsx" -o -name "*.py" -o -name "*.swift" \) 2>/dev/null | head -30

echo -e "\n=== Size Analysis ==="
find "$TARGET" -type f \( -name "*.c" -o -name "*.h" -o -name "*.ts" -o -name "*.tsx" -o -name "*.py" -o -name "*.swift" \) -exec wc -l {} + 2>/dev/null | sort -n | tail -20
```

Provide deep analysis including:
1. Architecture overview
2. Key patterns and design decisions
3. Potential issues or tech debt
4. Recommendations for improvement
