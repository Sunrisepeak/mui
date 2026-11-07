-- The film's tally counter in Lua (after example 03), as the code shot
-- shows it and runs it: the same tree as tally.ts, tally.cpp and tally.py.
local count = omui.signal(0)
local goal = omui.signal(10)
local share = omui.call("double", count)
  / omui.call("double", omui.call("max", goal, 1))

assert(omui.mount("main", omui.Column { padding = 24, gap = 12,
  omui.Text { id = "count", text = omui.call("string", count), style = "display" },
  omui.Progress { id = "progress", label = "Towards the goal",
                  value = omui.call("clamp", share, 0.0, 1.0) },
  omui.Text { id = "status", text = omui.select(omui.ge(count, goal),
              omui.fmt("Goal of {} reached", goal),
              omui.fmt("{} to go", goal - count)) },
  omui.Row { id = "controls", gap = 8,
    omui.Button { id = "minus", text = "−1", disabled = omui.eq(count, 0),
      on_activated = function()
        count:update(function(v) return math.max(v - 1, 0) end) end },
    omui.Button { id = "plus", text = "+1", variant = "primary",
      on_activated = function() count:update(function(v) return v + 1 end) end },
    omui.Button { id = "reset", text = "Reset", variant = "ghost",
      on_activated = function() count:set(0) end },
  },
}))
