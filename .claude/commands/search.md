# Search Codebase

Search for: $ARGUMENTS

```bash
echo "=== Searching for: $ARGUMENTS ==="
grep -rn --include="*.c" --include="*.h" --include="*.ts" --include="*.tsx" --include="*.js" --include="*.jsx" --include="*.py" --include="*.swift" --include="*.md" "$ARGUMENTS" . 2>/dev/null | head -30
```

Summarize where this appears and in what context.
