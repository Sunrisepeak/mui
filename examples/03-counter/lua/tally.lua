-- The tally counter in Lua: the same tree as src/tally.cpp and
-- ui/tally.view.omui.json (the tests check it).
local count = omui.signal(0)
local goal = omui.signal(10)

local function Tally()
  return omui.Column { padding = 24, gap = 12,
    omui.Text { id = "count", text = omui.call("string", count), style = "display" },
    omui.Progress { id = "progress", label = "Towards the goal",
      value = omui.call("clamp", omui.call("double", count) / omui.call("double", omui.call("max", goal, 1)), 0.0, 1.0) },
    omui.Text { id = "status",
      text = omui.select(omui.ge(count, goal), omui.fmt("Goal of {} reached", goal), omui.fmt("{} to go", goal - count)) },
    omui.Row { id = "controls", gap = 8,
      omui.Button { id = "minus", text = "−1", disabled = omui.eq(count, 0),
        on_activated = function() count:update(function(v) return math.max(v - 1, 0) end) end },
      omui.Button { id = "plus", text = "+1", variant = "primary",
        on_activated = function() count:update(function(v) return v + 1 end) end },
      omui.Button { id = "reset", text = "Reset", variant = "ghost", on_activated = function() count:set(0) end },
    },
    omui.Row { id = "goal", gap = 8,
      omui.Text { text = "Goal" },
      omui.Slider { id = "value", label = "Goal", max = 100, min = 1, step = 1, value = goal },
    },
  }
end

assert(omui.mount("main", Tally()))
