// Who else acts here, and what each did (agent connect §3.2): the agent the
// person linked — its link's state, its name — and the actions of the actors
// it follows (agents, by default) from the runtime's journal, newest last,
// each timed from their first.
// Each record is a node a screen reader or an agent reads; while it is the
// last its history holds, it is taken back from here (its scope's undo).
// Hovering or focusing a record points out the node it acted on: an outline
// over the frame, not a cursor. Ending the link is the person's own (S-10):
// the application's control, not an action here an agent could take.
// Later, other people working on the same thing.
import { defineContract } from "openmui/define";

export default defineContract({
  actor_log: {
    role: "log",
    spec: "0.1",
    doc: "Who else acts here and what each did: the agent's link and its actions, each taken back from here while it is the last.",
    props: {
      actors: {
        type: "string",
        default: "agent:",
        doc: "Whose actions it lists: actors whose names begin with this (`agent:` every agent, `agent:guide` one).",
      },
      link: {
        type: "bool",
        default: true,
        doc: "Its first line says the person's agent link: waiting with its code, paired with whom, lost, expired.",
      },
      limit: { type: "i64", default: 50, doc: "The records it shows, the newest." },
      phrases: {
        type: "any",
        doc: "Its words in another language: a map from idle, waiting, paired, lost, expired, stopped, empty, undo, asked, refused to text ({code} and {agent} filled in).",
      },
    },
    events: { undone: { args: { entry: "i64" } } },
    keys: { Up: "previous", Down: "next" },
    commands: { previous: { doc: "Move focus to the record before." }, next: { doc: "Move focus to the record after." } },
    parts: { status: {}, record: {}, undo: {} },
  },
});
