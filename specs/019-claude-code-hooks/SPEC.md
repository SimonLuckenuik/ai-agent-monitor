# 019 Claude Code hooks that drive the clock

Status: Draft (proposed 2026-10-30 from CLAUDE.md's earlier next steps, not approved)

## Problem

Agents must remember to message the clock. Claude Code hooks could do it on their own:
a message when Claude waits for the owner, when it finishes, and clearing it when the
owner answers.

## Goals

1. Hooks in the plugin: Notification → `waiting`, Stop → `done`, UserPromptSubmit →
   clear, through `/api/message`.
2. Off unless the owner configures the clock's address.

## Open questions

- Per-project titles; how long `done` stays; whether to use the Agents board.
