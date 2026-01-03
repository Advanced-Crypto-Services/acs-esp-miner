---
name: pre-pr-check
description: Run pre-flight checks before creating a PR
---
# Pre-PR Check

Run all quality checks before creating a pull request.

## Checks

| Check | Type |
|-------|------|
| Lint | Blocking |
| Types | Blocking |
| Tests | Blocking |
| Format | Warning |

## Process

1. Check for uncommitted changes
2. Run each check in sequence
3. Report results in table format
4. Summarize pass/fail

## Output

```
## Pre-PR Check Results

| Check | Status |
|-------|--------|
| Lint | ✅ Passed |
| Types | ✅ Passed |
| Tests | ✅ 47 passed |

**Result:** Ready for PR
```
