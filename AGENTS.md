# AGENTS.md

This repository contains mostly C code.

- Default to analysis and suggestions only. **Do not modify files unless explicitly asked.**
- Only inspect code relevant to the user's prompt.
- When suggesting optimizations, preserve behavior and APIs unless requested otherwise.
- Prefer small, local changes over broad refactoring.
- Consider runtime, code size, RAM/stack usage, numerical precision, portability, and undefined behavior.
- Do not introduce dependencies or assume floating-point hardware/POSIX support without evidence.
- Explain proposed changes and their trade-offs before any implementation.
- Do not perform unrelated cleanup or formatting.
