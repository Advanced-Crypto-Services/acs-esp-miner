# Sync with Remote

Sync current branch with remote, handling common scenarios.

```bash
echo "=== Fetching ==="
git fetch --all --prune

echo -e "\n=== Status ==="
git status

echo -e "\n=== Behind/Ahead ==="
git rev-list --left-right --count HEAD...@{upstream} 2>/dev/null || echo "No upstream set"
```

Based on the status:
- If behind: suggest `git pull --rebase`
- If ahead: suggest `git push`
- If diverged: suggest rebase strategy
- If conflicts: help resolve them
