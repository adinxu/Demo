## Project Structure

- `src/` - 主要源代码目录
- `doc/` - 过程文档存放
- `specs/` - 规范文档，见下方Stage-Gated Workflow (spec/plan/do)章节
- `plans/` - 计划文档，见下方Stage-Gated Workflow (spec/plan/do)章节
- `src/ref/` - 此文件夹下存放不同操作系统平台的不同交换芯片收发包参考代码，非本项目代码，仅供参考
- `src/ref/realtek` - 存放睿昱芯片收发包参考代码
- `src/ref/netforward` - 存放楠菲微芯片收发包参考代码
- `tests/` - 测试文件存放

## Coding Conventions

- 使用c语言
- 程序需具备良好的可读性，易于理解
- 程序需具备良好的可维护性，易于扩展
- 程序需具备良好的健壮性，能处理异常情况
- 程序需具备良好的性能
- 程序需具备良好的跨平台移植性，支持不同CPU架构(arm64、mips等)
- 文档使用中文输出，流程图(比如UML的状态机图、通信图、顺序图)优先使用mermaid语法绘制
- 若生产环境使用的特定平台特定芯片厂商提供的交叉编译链无法安装，则选择使用通用的特定平台交叉编译链进行编译验证

## Testing Guidelines

- 优先采用可平台移植的库函数来实现测试程序
- 测试程序具有良好的输出与结果总结

## Stage-Gated Workflow (spec/plan/do)

- Mode: Opt-in. The workflow applies only when the user explicitly uses `/spec`, `/plan`, or `/do`. Routine Q&A or trivial edits do not require these stages.
- Triggers: A message containing one of `/spec`, `/plan`, or `/do` activates or advances the workflow. Once active, stages must proceed in order with explicit user approval to advance.
- Guardrails:
  - Do not modify source code before `/do`. Documentation/spec files may be edited only in `/spec`.
  - Do not skip stages or proceed without user confirmation once the workflow is active.
  - If scope changes, return to the appropriate prior stage for approval.
  - Respect sandbox/approval settings for all actions.

- When to Use
  - Use the workflow for new features, structural refactors, multi-file changes, or work needing traceability.
  - Skip the workflow (no triggers) for routine Q&A, diagnostics, or one-off trivial edits.

- Entry Points and Prerequisites
  - `/spec` is the canonical entry point for new efforts.
  - `/plan` requires an approved `/spec`. If unclear which spec applies, pause and ask the user to identify the correct file(s) under `specs/`.
  - `/do` requires an approved `/plan`.

- `/spec` (Specifications; docs only)
  - Purpose: Capture a concrete, reviewable specification using spec-kit style.
  - Output: Markdown spec(s) under `specs/` (no code changes). Share a concise diff summary and links to updated files; wait for approval.
  - Style: Specs are canonical and final. Do not include change logs or “correction/更正” notes. Incorporate revisions directly so the document always reflects the current agreed state. Historical context belongs in PR descriptions, commit messages, or the conversation — not in the spec.
  - Recommended contents:
    - Problem statement and context
    - Goals and non-goals
    - Requirements and constraints (functional, UX, performance, security)
    - UX/flows and API/IPC contracts (as applicable)
    - Acceptance criteria and success metrics
    - Alternatives considered and open questions
    - Rollout/backout considerations and telemetry (if relevant)

- `/plan` (High-level Plan; docs only)
  - Purpose: Turn the approved spec into an ordered, verifiable implementation plan.
  - Inputs: Approved spec file(s) in `specs/`.
  - Ambiguity: If the relevant spec is unclear, pause and request clarification before writing the plan.
  - Style: Plans are canonical and should not include change logs or “correction/更正” notes. Incorporate revisions directly so the plan always reflects the current agreed state. Historical notes should live in PR descriptions, commit messages, or the conversation.
  - Output:
    - An ordered plan via `update_plan` (short, verifiable steps; Task is merged into Plan and tracked here).
    - A plan document in `plans/` named `YYYY-MM-DD-short-title.md`, containing:
      - Scope and links to authoritative spec(s)
      - Assumptions and out-of-scope items
      - Phases/milestones mapped to acceptance criteria
      - Impacted areas, dependencies, risks/mitigations
      - Validation strategy (tests/lint/build) and rollout/backout notes
      - Approval status and next stage
  - Handoff: Await user approval of the plan before `/do`.

- `/do` (Execution)
  - Purpose: Implement approved plan steps with minimal, focused changes and file operations.
  - Actions:
    - Use `apply_patch` for file edits; group related changes and keep diffs scoped to approved steps.
    - Provide concise progress updates and a final summary of changes.
    - Validate appropriately: run some relevant tests.
    - If material changes to the plan are needed, pause and return to `/plan` (or `/spec`) for approval.
  - Output: Implemented changes, validation results, and a concise change summary linked to the plan checklist.

### Plans Directory

- Location: `plans/` at the repository root.
- Filename: `YYYY-MM-DD-short-title.md` (kebab-case title; consistent dating).
- Style: Plan docs are the canonical source of truth for the implementation approach; avoid embedding change logs or “correction/更正” notes. Update the plan in place as decisions evolve.
- Contents:
  - Title and summary
  - Scope and linked specs (paths under `specs/`)
  - Assumptions / Out of scope
  - Step-by-step plan (short, verifiable)
  - Validation strategy (tests/lint/build)
  - Approval status and next stage
- Process:
  - During `/plan`, create or update the relevant file in `plans/` and share a short summary in the conversation. Await approval before `/do`.