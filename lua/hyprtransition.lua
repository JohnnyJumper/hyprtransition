-- hyprtransition — purely visual workspace transitions for Hyprland's Lua config.
--
--     require("hyprtransition").setup()
--     dofile(os.getenv("HOME") .. "/.local/share/hyprtransition/hyprtransition.lua").setup()
--
-- Settings come from ~/.config/hyprtransition/config.lua; setup({ ... }) overrides them.
--
-- Hyprland fires `workspace.active` inside the switch, before anything is drawn.
-- We step back to the old workspace right there, spawn `hyprtransition` to cover
-- the monitor with a screenshot of it, and switch forward once the cover is up.

local M = {}

local defaults = {
	effect = "tear", -- a name, or a list to pick from at random
	duration = nil, -- ms; nil = the effect file's own "// duration:" line
	cursor = false,
	fallback_ms = 400, -- switch anyway if the overlay never appears
	disable_workspace_animation = true,
	bin = "hyprtransition",
}

local NAMESPACE = "hyprtransition"

-- ---------------------------------------------------------------- config

local function config_path()
	local base = os.getenv("XDG_CONFIG_HOME") or (os.getenv("HOME") .. "/.config")
	return base .. "/hyprtransition/config.lua"
end

local function report(message)
	hl.notification.create({ text = "hyprtransition: " .. message, timeout = 8000, color = "rgb(ff5555)" })
end

local function read_config()
	local path = config_path()
	if not io.open(path, "r") then
		return {}
	end
	local ok, config = pcall(dofile, path)
	if not ok then
		report(config)
		return {}
	end
	if type(config) ~= "table" then
		report(path .. " must return a table")
		return {}
	end
	return config
end

local function merge(target, source)
	for key, value in pairs(source) do
		target[key] = value
	end
end

-- ---------------------------------------------------------------- switching

local seen = {} -- monitor id -> workspace id last active there
local own_switch = false
local reveal = nil -- the forward switch, run once the overlay is up
local playing = false

local function switch_to(workspace_id)
	own_switch = true
	pcall(hl.dispatch, hl.dsp.focus({ workspace = workspace_id }))
	own_switch = false
end

local function chosen_effect()
	local effect = M.effect
	if type(effect) == "table" then
		return effect[math.random(#effect)]
	end
	return effect
end

local function command_for(monitor)
	local parts = { M.bin, "-e", chosen_effect(), "-o", monitor.name }
	if M.duration then
		table.insert(parts, "-d " .. M.duration)
	end
	if M.cursor then
		table.insert(parts, "-c")
	end
	return table.concat(parts, " ")
end

local function after(ms, fn)
	hl.timer(fn, { timeout = ms, type = "oneshot" })
end

local function play(monitor, then_switch)
	reveal = then_switch
	playing = true
	hl.exec_cmd(command_for(monitor))

	after(M.fallback_ms, function()
		if reveal == then_switch then
			reveal = nil
			then_switch()
		end
	end)
	after((M.duration or 700) + 100, function()
		playing = false
	end)
end

local function on_overlay_opened(layer)
	local ok, namespace = pcall(function()
		return layer.namespace
	end)
	if ok and namespace == NAMESPACE and reveal then
		local switch = reveal
		reveal = nil
		switch()
	end
end

local function on_workspace_active(workspace)
	if own_switch then
		return
	end
	local monitor = workspace.monitor
	if not monitor then
		return
	end
	local previous = seen[monitor.id]
	seen[monitor.id] = workspace.id

	local changed = previous and previous ~= workspace.id and not workspace.special
	if not changed or not M.enabled or playing then
		return
	end

	local target = workspace.id
	switch_to(previous)
	play(monitor, function()
		switch_to(target)
	end)
end

-- ---------------------------------------------------------------- public

M.enabled = true

function M.setup(overrides)
	merge(M, defaults)
	merge(M, read_config())
	merge(M, overrides or {})

	hl.layer_rule({ name = "hyprtransition-noanim", match = { namespace = "^" .. NAMESPACE .. "$" }, no_anim = true })
	if M.disable_workspace_animation then
		hl.animation({ leaf = "workspaces", enabled = false, speed = 1, bezier = "default" })
	end

	for _, monitor in ipairs(hl.get_monitors()) do
		if monitor.active_workspace then
			seen[monitor.id] = monitor.active_workspace.id
		end
	end
	hl.on("workspace.active", on_workspace_active)
	hl.on("layer.opened", on_overlay_opened)

	HyprTransition = M -- for `hyprctl dispatch` snippets
	return M
end

return M
