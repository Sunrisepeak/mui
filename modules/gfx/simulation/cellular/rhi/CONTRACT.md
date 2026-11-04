# Cellular RHI execution, v0.1

| Boundary | Contract |
|---|---|
| Preparation | Requires a prepared v1 CPU plan and compute capability. Copies the plan, creates a pipeline/rule buffer and submits one rule upload. `ready()` names that submission; later work on the same queue observes it in order. Unsupported formats/resources return RHI errors. |
| Ownership | Device outlives kernel and field uses. Kernel owns only pipeline/rule buffer; host owns state buffers, queues, state revision, checkpoints and lifetime. A borrowed Field must truthfully name its device owner; raw handle numbers do not identify a device globally. |
| Record | Validate shape, channels, boundary, live resources, size and usages before appending. Any validation error leaves the existing command list/payload unchanged. The caller must append at a legal pass boundary. RHI validates the whole list on submit. |
| Execution | Input/output are separate fields. One invocation writes one cell using immutable old data; no race/order-dependent neighborhood writes. Host records ping-pong fields and handles publication. CPU/GPU state equality is exact for the integer operators tested. |
| Failures | Record atomicity is not GPU transactionality. Device creation, allocation, dispatch limits, asynchronous backend errors and loss can still fail. Host does not publish an unconfirmed result; restore/rebuild and replay from its authoritative checkpoint. No implicit cross-device copying or recovery. |
| Scope | Dense full-field dispatch only. CPU tile size, GPU workgroup size and render tile size are distinct. No sparse active set, physical halo transfer, generic compute scheduler, frame-graph auto-registration, GPU timestamps, field renderer or Noita performance guarantee. |

The optional adapter depends on CPU rules, RHI, trace and shader build rules. Neither CPU simulation nor RHI depends on this adapter at runtime. Backend test-only dependencies exercise the domain contract; selecting a plain RHI backend in an application does not pull in this test suite.
