-- hyprtransition — purely visual workspace transitions for Hyprland's Lua config.
--
-- Usage (anywhere in your hyprland.lua):
--
--     require("hyprtransition").setup()           -- system install (module is on the Lua path)
--
--     -- user install (nothing is placed in ~/.config/hypr, so load it by path):
--     dofile(os.getenv("HOME") .. "/.local/share/hyprtransition/hyprtransition.lua").setup()
--
-- Settings come from ~/.config/hyprtransition/config.lua (see config.example.lua);
-- anything passed to setup({ ... }) overrides the file, e.g. setup({ effect = "burn" }).
-- Remove the line to turn it off.
--
-- It knows nothing about your keys, your workspace count or your plugins: it
-- reacts to the workspace *changing*, however that happened (keys, scroll,
-- waybar, hyprctl, a plugin's dispatcher).
--
-- How: Hyprland fires `workspace.active` synchronously inside the switch,
-- before anything has been drawn. We step back to the old workspace right there
-- (invisible), spawn `hyprtransition` to screenshot it and cover the monitor
-- with a click-through overlay, and the moment Hyprland reports that overlay
-- as opened we switch forward underneath it. The effect then reveals it.
--
-- At runtime, from a bind or a terminal (a `hyprctl reload` resets to setup()):
--
--     hyprctl dispatch '(function() HyprTransition.enabled = not HyprTransition.enabled return hl.dsp.no_op() end)()'
--     hyprctl dispatch '(function() HyprTransition.effect = "burn" return hl.dsp.no_op() end)()'

local M = {}

local defaults = {
	effect = "tear", -- name in an effects dir, or a list to pick from at random
	duration = nil, -- ms; nil = the effect's own "// duration:" line
	cursor = false, -- include the mouse cursor in the captured screen
	fallback_ms = 400, -- if the overlay never shows up (binary missing?), switch anyway after this
	disable_workspace_animation = true, -- the effect replaces Hyprland's own slide
	bin = "hyprtransition", -- the binary; anything your shell can find
}

-- ---------------------------------------------------------------- config file

local function config_dir()
	local xdg = os.getenv("XDG_CONFIG_HOME")
	if xdg then
		return xdg .. "/hyprtransition"
	end
	return (os.getenv("HOME") or "~") .. "/.config/hyprtransition"
end

-- config.lua must `return { ... }`; a broken file is reported as a notification
-- and ignored rather than breaking your whole Hyprland config.
local function read_config()
	local path = config_dir() .. "/config.lua"
	local f = io.open(path, "r")
	if not f then
		return {}
	end
	f:close()
	local ok, cfg = pcall(dofile, path)
	if not ok or type(cfg) ~= "table" then
		hl.notification.create({
			text = "hyprtransition: " .. (ok and (path .. " must return a table") or tostring(cfg)),
			timeout = 8000,
			color = "rgb(ff5555)",
		})
		return {}
	end
	return cfg
end

-- ---------------------------------------------------------------- effect

local last = {} -- monitor id -> workspace id we last saw active there
local suppress = false -- our own switches must not re-trigger us
local pending = nil -- the forward switch waiting for the overlay to show up
local busy = false -- an effect is already on screen; don't stack another

local function switch(id)
	suppress = true
	pcall(hl.dispatch, hl.dsp.focus({ workspace = id }))
	suppress = false
end

local function pick_effect()
	local e = M.effect
	if type(e) == "table" then
		return e[math.random(#e)]
	end
	return e
end

local function with_effect(mon, action)
	pending = action
	busy = true
	local cmd = string.format("%s -e %s -o %s", M.bin, pick_effect(), mon.name)
	if M.duration then
		cmd = cmd .. " -d " .. M.duration
	end
	if M.cursor then
		cmd = cmd .. " -c"
	end
	hl.exec_cmd(cmd)

	hl.timer(function()
		if pending == action then
			pending = nil
			action()
		end
	end, { timeout = M.fallback_ms, type = "oneshot" })

	hl.timer(function()
		busy = false
	end, { timeout = (M.duration or 700) + 100, type = "oneshot" })
end

local function on_workspace_active(ws)
	if suppress then
		return
	end
	local mon = ws.monitor
	if not mon then
		return
	end
	local prev = last[mon.id]
	last[mon.id] = ws.id
	-- first sighting of this monitor, no actual change, or a special workspace: nothing to do
	if not prev or prev == ws.id or ws.special then
		return
	end
	if not M.enabled or busy then
		return
	end

	-- Nothing has been drawn yet: step back, cover the old screen, then go forward under the cover.
	local target = ws.id
	switch(prev)
	with_effect(mon, function()
		switch(target)
	end)
end

-- ---------------------------------------------------------------- public

M.enabled = true

function M.setup(opts)
	for k, v in pairs(defaults) do
		M[k] = v
	end
	for k, v in pairs(read_config()) do
		M[k] = v
	end
	for k, v in pairs(opts or {}) do
		M[k] = v
	end

	hl.layer_rule({
		name = "hyprtransition-noanim",
		match = { namespace = "^hyprtransition$" },
		no_anim = true,
	})

	if M.disable_workspace_animation then
		hl.animation({ leaf = "workspaces", enabled = false, speed = 1, bezier = "default" })
	end

	for _, mon in ipairs(hl.get_monitors()) do
		if mon.active_workspace then
			last[mon.id] = mon.active_workspace.id
		end
	end

	hl.on("workspace.active", on_workspace_active)

	hl.on("layer.opened", function(layer)
		local got, ns = pcall(function()
			return layer.namespace
		end)
		if got and ns == "hyprtransition" and pending then
			local action = pending
			pending = nil
			action()
		end
	end)

	HyprTransition = M -- reachable from `hyprctl dispatch` Lua snippets
	return M
end

return M
