# Create Pull Request

Create a well-documented pull request.

## First, gather context:

```bash
echo "=== Current Branch ==="
git branch --show-current

echo -e "\n=== Base Branch ==="
git symbolic-ref refs/remotes/origin/HEAD 2>/dev/null | sed "s@^refs/remotes/origin/@@" || echo "main"

echo -e "\n=== Commits to Include ==="
BASE=$(git symbolic-ref refs/remotes/origin/HEAD 2>/dev/null | sed "s@^refs/remotes/origin/@@" || echo "main")
git log origin/${BASE}..HEAD --oneline

echo -e "\n=== Files Changed ==="
git diff origin/${BASE}..HEAD --stat
```

## Then:
1. Analyze the commits and changes
2. Generate a PR title following conventional commits (fix:, feat:, refactor:, docs:, chore:)
3. Write a clear description with Summary, Changes, and Testing sections
4. Create the PR with: `gh pr create --title "..." --body "..."`
