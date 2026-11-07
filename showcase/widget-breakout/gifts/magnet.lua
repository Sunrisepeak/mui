-- The magnet (Lua): the paddle holds a returned ball, and you aim again.
local catches = 3 -- ← tune: the returns it holds, 1 … 8

omui.verb("gift.magnet", { effect = "read", summary = "What the magnet does" }, function()
  return { magnet_catches = catches }
end)
