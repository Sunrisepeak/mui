-- The theme preview: choose a theme, a density and the motion, and see every
-- common component, the colour tokens and the text styles under them. The
-- themes are data — openmui's light, dark, compact and reduced_motion, and
-- this application's own brand (themes/brand.theme.omui.toml), which the
-- player reloads when it is saved.
local scheme = omui.signal("light")
local compact = omui.signal(false)
local calm = omui.signal(false)
local current = omui.signal(omui.themes().current)
local problem = omui.signal("")

-- The composition the controls say: a theme, then overlays.
local function compose()
  local layers = { scheme:get() }
  if compact:get() then layers[#layers + 1] = "compact" end
  if calm:get() then layers[#layers + 1] = "reduced_motion" end
  local ok, err = omui.theme(layers)
  problem:set(ok and "" or err)
  current:set(omui.themes().current)
end

local swatches = { "background", "surface", "surface_variant", "accent", "accent_subtle", "text", "text_muted", "border", "success", "warning", "danger" }
local styles = { "display", "title", "heading", "body", "label", "caption", "code" }

local function Controls()
  return omui.Row { id = "controls", gap = 16, align_items = "center",
    omui.Select { id = "scheme", label = "Theme", options = { "light", "dark", "brand" }, labels = { "Light", "Dark", "Brand" }, value = scheme,
      on_changed = function(e) scheme:set(e.value); compose() end },
    omui.Toggle { id = "compact", label = "Compact", checked = compact,
      on_changed = function(e) compact:set(e.checked); compose() end },
    omui.Toggle { id = "calm", label = "Reduced motion", checked = calm,
      on_changed = function(e) calm:set(e.checked); compose() end },
  }
end

local function Gallery()
  return omui.Column { id = "gallery", gap = "m",
    omui.Row { id = "buttons", gap = "s",
      omui.Button { id = "primary", text = "Save", variant = "primary" },
      omui.Button { id = "secondary", text = "Cancel" },
      omui.Button { id = "ghost", text = "More", variant = "ghost" },
      omui.Button { id = "off", text = "Disabled", disabled = true },
    },
    omui.TextBox { id = "field", label = "Name", placeholder = "Ada Lovelace" },
    omui.Row { id = "choices", gap = "l", align_items = "center",
      omui.Checkbox { id = "check", label = "Remember me", checked = true },
      omui.Toggle { id = "toggle", label = "Notifications", checked = true },
    },
    omui.Slider { id = "slider", label = "Volume", min = 0, max = 100, value = 60 },
    omui.Progress { id = "progress", label = "Upload", value = 0.4 },
  }
end

local function Swatches()
  local row = { id = "swatches", gap = "xs" }
  for _, token in ipairs(swatches) do
    row[#row + 1] = omui.Box { id = token, background = token, width = 44, height = 28, radius = "small",
      border_width = 1, border_color = "border", label = token }
  end
  return omui.Row(row)
end

local function Type()
  local column = { id = "type", gap = "xs" }
  for _, style in ipairs(styles) do
    column[#column + 1] = omui.Text { id = style, text = style:gsub("^%l", string.upper) .. " — the quick brown fox", style = style }
  end
  return omui.Column(column)
end

local function Preview()
  return omui.Column { padding = "xl", gap = "l", background = "background",
    Controls(),
    omui.Text { id = "current", text = omui.fmt("Composed: {}", current), style = "caption" },
    omui.when(omui.ne(problem, ""), { omui.Text { id = "problem", text = problem } }),
    Gallery(),
    Swatches(),
    Type(),
  }
end

assert(omui.mount("main", Preview()))
