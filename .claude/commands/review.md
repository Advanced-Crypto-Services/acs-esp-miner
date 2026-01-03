# Code Review

Review recent changes or specific files.

Target: $ARGUMENTS

```bash
if [ -z "$ARGUMENTS" ]; then
  echo "=== Uncommitted Changes ==="
  git diff
else
  echo "=== Reviewing: $ARGUMENTS ==="
  if [ -f "$ARGUMENTS" ]; then
    cat "$ARGUMENTS"
  else
    git diff "$ARGUMENTS"
  fi
fi
```

Perform a thorough code review checking:
1. **Correctness**: Logic errors, edge cases, error handling
2. **Security**: Input validation, injection risks, auth issues
3. **Performance**: Inefficiencies, unnecessary allocations
4. **Maintainability**: Naming, complexity, documentation
5. **Style**: Consistency with project patterns

Format as: 🟢 Good | 🔴 Must Fix | 🟡 Suggestion
