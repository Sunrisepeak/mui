-- Slow motion (Lua): the same game time saved, however slow.
local percent = 50 -- ← tune: the clock's speed, 25 … 90 %
local saved = 4    --   seconds of game time saved

omui.verb("gift.slow", { effect = "read", summary = "What the slow-motion gift does" }, function()
  return { slow_percent = percent, slow_steps = math.floor(saved / (1 - percent / 100) * 120 + 0.5) }
end)
