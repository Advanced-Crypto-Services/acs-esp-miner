# Project Status

Get an overview of the current project state.

## Run these commands and summarize the results:

```bash
echo "=== Git Status ==="
git status --short

echo -e "\n=== Recent Commits ==="
git log --oneline -10

echo -e "\n=== Branch Info ==="
git branch -vv

echo -e "\n=== Uncommitted Changes ==="
git diff --stat
```

Provide a concise summary of:
1. Current branch and sync status
2. Uncommitted changes
3. Recent activity
