---
name: create-pr
description: Create a pull request with pre-flight checks
---
# Create Pull Request

Create a well-documented pull request.

## Process

1. Run `/pre-pr-check`
2. Get base branch: `git symbolic-ref refs/remotes/origin/HEAD`
3. Analyze commits: `git log origin/$BASE..HEAD --oneline`
4. Generate PR description
5. Create with `gh pr create`

## PR Title Format

- `fix: description` - Bug fixes
- `feat: description` - Features
- `refactor: description` - Refactoring
- `docs: description` - Documentation
- `chore: description` - Maintenance

## Flags

- `/create-pr` - Full checks + create
- `/create-pr --quick` - Skip tests
- `/create-pr --force` - Ignore failures
