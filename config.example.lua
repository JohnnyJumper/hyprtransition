-- hyprtransition settings — copy to ~/.config/hyprtransition/config.lua
-- Every field is optional; the values shown are the defaults. This is plain Lua,
-- so you can compute things (e.g. effect = os.getenv("HOSTNAME") == "laptop" and "fade" or "tear").

return {
	-- Effect to play: a file in an effects dir. A list picks one at random per switch,
	-- e.g. { "tear", "burn", "tiles" }.
	effect = "tear",

	-- Duration in ms. nil = each effect file's own "// duration:" line.
	duration = nil,

	-- Include the mouse cursor in the captured screen.
	cursor = false,

	-- If the overlay never appears (binary missing?), switch anyway after this many ms.
	fallback_ms = 400,

	-- The effect replaces Hyprland's own workspace slide; false keeps both.
	disable_workspace_animation = true,

	-- The binary to run, if it is not simply on your PATH.
	bin = "hyprtransition",
}
