# Find TODOs

Search for TODO, FIXME, HACK, and XXX comments in the codebase.

```bash
echo "=== TODOs and FIXMEs ==="
grep -rn --include="*.c" --include="*.h" --include="*.ts" --include="*.tsx" --include="*.js" --include="*.py" --include="*.swift" -E "(TODO|FIXME|HACK|XXX):" . 2>/dev/null | head -50 || echo "No TODOs found"
```

Organize the results by priority (FIXME > TODO > HACK > XXX) and summarize.
