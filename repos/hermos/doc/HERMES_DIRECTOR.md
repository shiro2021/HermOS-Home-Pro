# Hermes Core Director contract

## Role and boundary

Hermes is a HermOS child component started by Genode `init`. It does not replace Genode `core`, `init`, or NOVA. The parent composition controls its memory/capability quota and routes its sessions. The first director has no Block/File_system, device, model/network, or disk-writing session route.

## Implemented protocol increment (ABI v1)

The source-grounded first version is a custom `Hermes` Genode session. It exposes health, shared request and response dataspaces, and `submit(request_size)`. The dataspaces hold the fixed-layout `Request`/`Response` records in `include/hermos_director/session.h`; they are **not XML**. Each record identifies ABI version, operation, task ID, bounded goal, state, plan, and execution status. Goal capacity is 128 bytes and plan capacity is 192 bytes.

The session capability authorizes bounded requests. The composition routes no block, filesystem, raw-disk, device, model, or network session to the director. Intents without a registered handler return `AWAITING_CLARIFICATION` with execution `NOT_IMPLEMENTED`. Unknown operations return `UNSUPPORTED`; malformed ABI/size/input is rejected.

### V8 Aladar first operational procedure

The static registry contains `hermes.system-status@1`, an exact-match,
read-only deterministic procedure for `Show system status`. It declares no
capabilities and has no side effects. Hermes logs `RUNNING`, `VERIFYING`, and
the terminal state. Its independent check calls the director health method;
only a passing check produces `SUCCEEDED` / `VERIFIED`. The integration client
checks the task id, selected mode, terminal state, execution result, and
procedure output. This is one real workflow, not an AI agent, dynamic worker,
persistent task store, or general intent interpreter.

No model backend is configured, so AI-dependent requests remain unavailable
or awaiting clarification. No tool broker, approval UI, cancellation, task
history, or worker lifecycle is implemented.

## Task state

The implemented path supports director health, proposal responses, one
verified read-only deterministic procedure, and unsupported-operation
rejection. Records are not persisted. Approval, worker lifecycle, AI reasoning,
recovery, cancellation, and general successful execution are not implemented.

## Failure behavior

If the director fails before announcing `Hermes`, the startup composition must not present the desktop as healthy. The initial integration test must fail because the client cannot obtain the `Hermes` session. Runtime restart/degraded UI and recovery policy remain a later milestone. A Genode log message is evidence of boot state, not a substitute for a response/health interaction test.

## Later contract increments

1. Add task IDs, event history and cancellation with bounded memory/resource quotas.
2. Expand the static deterministic procedure registry with declared preconditions, effects, and independent completion checks.
3. Add a separately routed policy/approval broker and exercise denied as well as approved requests.
4. Add worker lifecycle management only where the Genode 26.08 parent/child APIs provide enforceable capability revocation and cleanup.
5. Add an AI backend interface with explicit unavailable behavior; do not fabricate model output.
6. Bind the desktop and Agents & Workers panel to this protocol and display only returned runtime state. The current desktop shell does connect and query health at startup; the framebuffer/interactive display gate remains unresolved.

Any deviation from the first protocol proposal must be recorded here and in `DECISIONS.md` before implementation.
