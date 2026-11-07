-- A tour of the wall through the semantic API — the same in tour.ts and
-- tour.py (the tests check they record the same and leave the same wall).
-- Each step's result is recorded with the verb `tour.record`.
local wall = "main/wall"
local function record(step, value) omui.invoke("tour.record", { step = step, value = value }) end
-- A call with nothing to say answers true; a refusal, nil and "code: why".
local function data(v) if v == true then return nil end return v end
local function refusal(f)
  local r, err = f()
  if err then return err:match("^([%w%-]+):") or err end
  return "done"
end

-- Find a document by name, anywhere on the wall; how many workspaces.
local using = omui.act { node = wall, action = "find", args = { query = { name = "using" } } }
record("using", using)
record("workspaces", #omui.act { node = wall, action = "find", args = { query = { kind = "workspace" } } })
-- Fly to it (it goes live), move it, choose it.
record("focus", data(omui.act { node = wall, action = "focus_node", args = { key = using[1] } }))
record("move", data(omui.act { node = wall .. "/" .. using[1], action = "move_to", args = { x = 4000, y = 3000 } }))
record("select", data(omui.act { node = wall .. "/" .. using[1], action = "select" }))
-- What is near where it went, nearest first.
record("near", omui.act { node = wall, action = "find", args = { query = { near = { 4000, 3000 }, radius = 900 } } })
-- Two windows written into, then not.
record("stream", omui.invoke("wall.stream", { windows = 2, rate = 50 }))
record("stop", data(omui.invoke("wall.stop")))
-- What the wall refuses: a window has no `delete` here.
record("refused", refusal(function() return omui.act { node = wall .. "/" .. using[1], action = "delete", answer = "yes" } end))
-- Back to the whole wall.
record("overview", data(omui.act { node = wall, action = "camera_preset", args = { name = "overview" } }))
