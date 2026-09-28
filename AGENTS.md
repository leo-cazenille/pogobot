# AGENTS.md

This repository contains mostly C code.

- Default to analysis and suggestions only. **Do not modify files unless explicitly asked.**
- You may stage and commit task-related changes as needed; leave unrelated working-tree changes out of those commits.
- Only inspect code relevant to the user's prompt.
- When suggesting optimizations, preserve behavior and APIs unless requested otherwise.
- When asked to modify a code file to add new content, add relevant comments related to the added/modified code.
- Prefer small, local changes over broad refactoring.
- Consider runtime, code size, RAM/stack usage, numerical precision, portability, and undefined behavior.
- Do not introduce dependencies or assume floating-point hardware/POSIX support without evidence.
- Explain proposed changes and their trade-offs before any implementation.
- Do not perform unrelated cleanup or formatting.

- `docs/index.md` should provide a compact map of project documentation.
- `docs/current_state.md` should record:
  - what has been inspected;
  - what is understood;
  - what remains unknown;
  - currently working analyses;
  - current scientific decisions;
  - known data limitations;
  - next concrete tasks.
- Keep `current_state.md` concise and update it after meaningful milestones.
- For each completed IR upload software phase, record its final implementation
  commit hash and hardware validation status in `docs/ir_upload_reliability_plan.md`.
