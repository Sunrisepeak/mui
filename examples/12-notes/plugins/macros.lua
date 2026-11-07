-- Text macros, as verbs: any script calls them (logic/main.ts does, from
-- its toolbar and whenever it keeps a note), and so may an agent. Each takes
-- the note's text and answers the new text (or its title); none keeps any
-- state.

-- "## 2026-10-27" under the text: a day's heading. The date is the caller's
-- (the program's clock, or a test's).
assert(omui.verb("macro.today", { summary = "Append a heading for a day", effect = "reversible" }, function(a)
  local text = a.text or ""
  local sep = (text == "" or text:sub(-1) == "\n") and "" or "\n"
  return text .. sep .. "\n## " .. a.date .. "\n"
end))

-- "- [ ] " on a new line: a task to write.
assert(omui.verb("macro.task", { summary = "Append a task", effect = "reversible" }, function(a)
  local text = a.text or ""
  local sep = (text == "" or text:sub(-1) == "\n") and "" or "\n"
  return text .. sep .. "- [ ] " .. (a.task or "")
end))

-- A note's title: its first heading, or its first line.
assert(omui.verb("macro.title", { summary = "A note's title", effect = "read" }, function(a)
  local text = a.text or ""
  local heading = text:match("^#+%s+([^\n]+)") or text:match("\n#+%s+([^\n]+)")
  local first = heading or text:match("^%s*([^\n]+)") or "Untitled"
  return (first:gsub("%s+$", ""))
end))
