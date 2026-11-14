// Background tasks (W8, PPet §2.1): work an operation started without
// waiting for it — making, evaluating, preparing — each with a handle the
// moment it is asked for. The interface keeps running, input keeps coming,
// requests keep being answered while it runs; its progress and its end can
// be read at any time and stay readable; a cancel is taken at once, and what
// a cancelled task would have committed never is. omui.agent:tasks
// implements them (TaskBroker); omui-gen ops makes the table and listings.
import { defineOps } from "omui/define";

export default defineOps({
  product: "task",
  module: "omui.agent",
  partition: "tasks",
  namespace: "omui::agent::tasks",
  doc: "Background tasks: started with a handle at once, their progress and end readable, cancelled without waiting, the interface running all along.",
  generated: "src/catalog",
  operations: [
    {
      name: "task.list",
      effect: "read",
      summary: "The background tasks, newest first: each one's id, what it is, who started it, its state (running, done, failed, cancelled), its progress and how long it has run",
      params: [{ name: "all", type: "bool", doc: "the ended ones too (default: running and the last ten ended)", optional: true }],
      result: "json",
      cost: "instant",
    },
    {
      name: "task.status",
      effect: "read",
      summary: "One task: its state, progress (0…1, and what it is doing), and once ended its result or why it failed",
      params: [{ name: "task", type: "string", doc: "a task's id" }],
      result: "json",
      cost: "instant",
    },
    {
      name: "task.cancel",
      effect: "reversible",
      summary: "Cancels a task: taken at once (it answers without waiting for the work to stop), nothing the task would have committed is, and the work is asked to stop",
      params: [{ name: "task", type: "string", doc: "a task's id" }],
      result: "json",
      cost: "instant",
      undo: "none needed: nothing it would have made was kept; start it again",
    },
  ],
});
