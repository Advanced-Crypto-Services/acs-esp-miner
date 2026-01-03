# AI Agent Setup

This project supports multiple AI development tools.

## Structure

```
.claude/              # Claude Code CLI
  commands/           # Slash commands (bash-executable)
  settings.json       # Permissions config

.cursor/              # Cursor IDE
  commands/           # Slash commands
  rules/              # Context rules (.mdc)

.factory/             # Factory AI
  droids/             # AI agents
  commands/           # Commands
  rules/              # Coding standards
  skills/             # Domain expertise

CLAUDE.md             # Primary context for Claude Code
AGENTS.md             # Context for Cursor/Factory
```

## Claude Code CLI

### Slash Commands
Run commands with `/project:status`, `/build:firmware`, etc.

Commands in `.claude/commands/` can execute bash:
- `$ARGUMENTS` placeholder for user input
- Bash blocks are executed automatically

### Permissions
Edit `.claude/settings.json` to allow/deny specific commands.

## Cursor IDE

### Rules
Create `.mdc` files in `.cursor/rules/` with glob patterns.

## Factory AI

### Droids
AI agents in `.factory/droids/` for specific tasks.
Invoke with `@droid-name`.

