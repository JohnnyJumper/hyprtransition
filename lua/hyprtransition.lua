-- hyprtransition — purely visual workspace transitions for Hyprland's Lua config.
--
-- Usage (last line of your hyprland.lua, after your keybinds):
--
--     require("hyprtransition").setup({           -- system install (module is on the Lua path)
--         mod = "ALT",                            -- modifier of your workspace keys
--         effect = "tear",                        -- or { "tear", "burn", "tiles" } to pick at random
--     })
--
--     -- user install (nothing is placed in ~/.config/hypr, so load it by path):
--     dofile(os.getenv("HOME") .. "/.local/share/hyprtransition/hyprtransition.lua").setup({ mod = "ALT" })
--
-- Remove that line to turn it off. All options and their defaults are in
-- `defaults` below. The module takes over `mod + 1..workspaces` and
-- `mod + 0` / `mod + 9` (next/prev), hides the overlay from your layer
-- animations, and disables the built-in `workspaces` animation (the effect
-- replaces it). The workspace switch itself is whatever you already use:
-- split-monitor-workspaces if it is loaded, plain workspace dispatch otherwise.
--
-- At runtime, from a bind or a terminal (a `hyprctl reload` resets to setup()):
--
--     hyprctl dispatch '(function() HyprTransition.enabled = not HyprTransition.enabled return hl.dsp.no_op() end)()'
--     hyprctl dispatch '(function() HyprTransition.effect = "burn" return hl.dsp.no_op() end)()'
--
-- How it works: the key spawns `hyprtransition`, which screenshots the focused
-- monitor and covers it with a click-through overlay showing that screenshot.
-- The moment Hyprland reports the overlay as opened, the real switch is
-- dispatched underneath it, and the effect reveals the new workspace.

local M = {}

local defaults = {
	mod = "ALT",
	effect = "tear", -- name in an effects/ dir, or a list to pick from at random
	duration = nil, -- ms; nil = the effect's own "// duration:" line
	workspaces = 5, -- keys 1..workspaces; must match split_monitor_workspaces.count if used
	bin = "hyprtransition", -- the binary; anything your shell can find
	bind_keys = true, -- false: don't touch binds, call HyprTransition.go(i) / cycle(dir) yourself
	fallback_ms = 400, -- if the overlay never shows up (binary missing?), switch anyway after this
	disable_workspace_animation = true,
}

-- ---------------------------------------------------------------- backend

local smw -- split-monitor-workspaces' Lua helpers, when the plugin is loaded
do
	local ok, p = pcall(function()
		return hl.plugin.split_monitor_workspaces
	end)
	smw = ok and p or nil
end

-- The plugin's helpers dispatch on their own and return a result table; the
-- core helpers return an HL.Dispatcher userdata that still needs dispatching.
local function run(result)
	if type(result) == "userdata" then
		hl.dispatch(result)
	end
end

local function goto_ws(i)
	if smw then
		run(smw.workspace(i))
	else
		run(hl.dsp.focus({ workspace = i }))
	end
end

local function cycle(dir)
	if smw then
		run(smw.cycle_workspaces(dir))
	else
		run(hl.dsp.focus({ workspace = dir == "next" and "e+1" or "e-1" }))
	end
end

local function target_id(mon, i)
	if smw then
		return mon.id * M.workspaces + i
	end
	return i
end

-- ---------------------------------------------------------------- effect

local pending = nil -- the switch waiting for the overlay to show up
local busy = false -- an effect is already on screen; don't stack another

local function pick_effect()
	local e = M.effect
	if type(e) == "table" then
		return e[math.random(#e)]
	end
	return e
end

local function with_effect(action)
	local mon = hl.get_active_monitor()
	if not M.enabled or busy or not mon then
		action()
		return
	end

	pending = action
	busy = true
	local cmd = string.format("%s -e %s -o %s", M.bin, pick_effect(), mon.name)
	if M.duration then
		cmd = cmd .. " -d " .. M.duration
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

-- ---------------------------------------------------------------- public

M.enabled = true

--- Switch to workspace i (1-based, on the focused monitor) with the effect.
function M.go(i)
	local mon = hl.get_active_monitor()
	-- already there: nothing to reveal, keep the original behaviour untouched
	if mon and mon.active_workspace and mon.active_workspace.id == target_id(mon, i) then
		goto_ws(i)
		return
	end
	with_effect(function()
		goto_ws(i)
	end)
end

--- Cycle to the "next" or "prev" workspace with the effect.
function M.cycle(dir)
	with_effect(function()
		cycle(dir)
	end)
end

local function rebind(key, fn)
	hl.unbind(key)
	hl.bind(key, fn)
end

function M.setup(opts)
	for k, v in pairs(defaults) do
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

	if M.bind_keys then
		for i = 1, M.workspaces do
			rebind(M.mod .. " + " .. i, function()
				M.go(i)
			end)
		end
		rebind(M.mod .. " + 0", function()
			M.cycle("next")
		end)
		rebind(M.mod .. " + 9", function()
			M.cycle("prev")
		end)
	end

	HyprTransition = M -- reachable from `hyprctl dispatch` Lua snippets
	return M
end

return M
