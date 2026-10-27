-- The CTS level S runner of Lua (spec/conformance/S/README.md): a scenario's
-- view built with the Lua builders, metamethods and closures, its steps done
-- through the Lua API. The test (lua_test.cpp, through openmui.cts) calls the
-- two verbs it registers: cts.load with the scenario, then cts.step with each
-- step's index, the runtime settling between them; a step's answer is its
-- record, or nothing.
local S, signals = nil, {}

local function pascal(name)
  return (name:gsub("^%l", string.upper):gsub("_(%l)", string.upper))
end

local function is_map(v)
  return type(v) == "table" and next(v) ~= nil and v[1] == nil
end

-- ── the view ──────────────────────────────────────────────────────────────
local binary = {
  ["+"] = function(a, b) return a + b end,
  ["-"] = function(a, b) return a - b end,
  ["*"] = function(a, b) return a * b end,
  ["/"] = function(a, b) return a / b end,
  ["%"] = function(a, b) return a % b end,
  ["=="] = omui.eq, ["!="] = omui.ne, ["<"] = omui.lt, ["<="] = omui.le, [">"] = omui.gt, [">="] = omui.ge,
  ["and"] = omui.and_, ["or"] = omui.or_,
}

local operand
operand = function(v, row)
  if type(v) ~= "table" then return v end
  if v.const ~= nil then return v.const end
  if v.signal then return signals[v.signal] end
  if v.row then return row[v.row] end
  local args = {}
  for i, a in ipairs(v.args or {}) do args[i] = operand(a, row) end
  if v.op == "not" then return omui.not_(args[1]) end
  if v.op == "neg" then return -args[1] end
  if v.op then return binary[v.op](args[1], args[2]) end
  if v.call then return omui.call(v.call, table.unpack(args)) end
  if v.fmt then return omui.fmt(v.fmt, table.unpack(args)) end
  if v.select then
    return omui.select(operand(v.select[1], row), operand(v.select[2], row), operand(v.select[3], row))
  end
  error("an operand cannot be this: " .. tostring(next(v)))
end

local action

local function value(v, event, args)
  if type(v) ~= "table" then return v end
  if v.const ~= nil then return v.const end
  if v.event then return event[v.event] end
  if v.row then return event.rows.row[v.row] end
  if v.arg then return args[v.arg] end
  if v.get then return signals[v.get]:get() end
  local out = {}
  for k, x in pairs(v) do out[k] = value(x, event, args) end
  return out
end

local element
element = function(e, row)
  if e.when ~= nil then
    local yes, no = {}, {}
    for i, c in ipairs(e["then"] or {}) do yes[i] = element(c, row) end
    for i, c in ipairs(e["else"] or {}) do no[i] = element(c, row) end
    return omui.when(operand(e.when, row), yes, no)
  end
  if e.each then
    local where = e.where and function(r) return operand(e.where, r) end or nil
    return omui.each(e.each, function(r) return element(e.body, r) end, where)
  end
  local t = {}
  for k, v in pairs(e.props or {}) do t[k] = operand(v, row) end
  if e.id then t.id = e.id end
  for event, actions in pairs(e.on or {}) do
    t["on_" .. event] = function(ev)
      for _, a in ipairs(actions) do action(a, ev) end
    end
  end
  for i, c in ipairs(e.children or {}) do t[i] = element(c, row) end
  return omui[pascal(e.type)](t)
end

-- ── actions ───────────────────────────────────────────────────────────────
local function find(rows, key)
  for i, r in ipairs(rows) do
    if r.key == key then return i end
  end
  error("no row " .. tostring(key))
end

-- `fuzzy.rank` through the generated binding: omui.native.fuzzy.rank.
local function native_function(name)
  local module, fn = name:match("^(.-)%.(.+)$")
  local m = omui.native[module]
  return m and m[fn]
end

action = function(a, event, args)
  local function val(v) return value(v, event, args) end
  if a.native then
    local call = {}
    for i, x in ipairs(a.args or {}) do call[i] = val(x) end
    local r = assert(native_function(a.native)(table.unpack(call, 1, #(a.args or {}))))
    signals[a.into]:set(r)
  elseif a.set ~= nil and a.to ~= nil then
    signals[a.set]:set(val(a.to))
  elseif a.add then
    local by, lo, hi = a.by, a.min, a.max
    signals[a.add]:update(function(v)
      v = v + by
      if lo then v = math.max(v, lo) end
      if hi then v = math.min(v, hi) end
      return v
    end)
  elseif a.invoke then
    local call = {}
    for k, x in pairs(a.args or {}) do call[k] = val(x) end
    assert(omui.invoke(a.invoke, call))
  elseif a.push then
    local row = {}
    for k, x in pairs(a.row) do row[k] = val(x) end
    assert(omui.model(a.push):push({ row }))
  elseif a.remove or a.change then
    local m = omui.model(a.remove or a.change)
    local rows = assert(m:rows())
    local i = find(rows, val(a.key))
    local replace = {}
    if a.change then
      local row = rows[i]
      for k, x in pairs(a.set) do row[k] = val(x) end
      replace = { row }
    end
    assert(m:splice(i, 1, replace))
  else
    error("an action cannot be this")
  end
end

-- ── the scenario ──────────────────────────────────────────────────────────
local function sorted_keys(t)
  local keys = {}
  for k in pairs(t or {}) do keys[#keys + 1] = k end
  table.sort(keys)
  return keys
end

local function code_of(err)
  return (tostring(err):match("^([^:]+)")) or tostring(err)
end

assert(omui.verb("cts.load", { summary = "Load a CTS scenario" }, function(scenario)
  S = scenario.scenario
  for _, name in ipairs(sorted_keys(S.signals)) do signals[name] = omui.signal(S.signals[name]) end
  for _, name in ipairs(sorted_keys(S.models)) do assert(omui.model(name):push(S.models[name])) end
  for _, name in ipairs(sorted_keys(S.verbs)) do
    local d = S.verbs[name]
    assert(omui.verb(name, { effect = d.effect or "reversible", answer = d.answer, summary = d.summary }, function(args)
      for _, a in ipairs(d["do"] or {}) do action(a, nil, args) end
      return value(d.result, nil, args)
    end))
  end
  assert(omui.mount("main", element(S.view)))
  return true
end))

assert(omui.verb("cts.step", { summary = "Do a step of the loaded CTS scenario" }, function(args)
  local i = args.step
  local st = S.steps[i + 1]
  if st.tree then
    local snap, err = omui.snapshot({ root = st.tree })
    if not snap then return { step = i, error = code_of(err) } end
    return { step = i, tree = snap.nodes }
  elseif st.act then
    local request = { node = st.act, action = st.action }
    if is_map(st.args) then request.args = st.args end
    local r, err = omui.act(request)  -- nil and why, or the action's data (false is data)
    if r == nil then return { step = i, error = code_of(err) } end
    return { step = i, act = "ok" }
  elseif st.invoke then
    local r, err = omui.invoke(st.invoke, st.args or {})
    if r == nil then return { step = i, error = code_of(err) } end
    return { step = i, invoke = r }
  elseif st.get then
    return { step = i, get = signals[st.get]:get() }
  elseif st.rows then
    return { step = i, rows = assert(omui.model(st.rows):rows()) }
  elseif st.native and not st.into then
    local fn = native_function(st.native)
    if not fn then return { step = i, error = "not-found" } end  -- the binding has no such function
    local given = st.args or {}
    local r, err = fn(table.unpack(given, 1, #given))
    if r == nil then return { step = i, error = code_of(err) } end
    return { step = i, native = r }
  end
  action(st)
  return nil
end))
