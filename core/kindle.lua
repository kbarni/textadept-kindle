-- Copyright 2024-2025 Mitchell. See LICENSE.

if not kindle then return end

kindle.last_intensity = 10
--- Toggles the front light.
-- If it's on, it saves the current intensity and turns it off.
-- If it's off, it restores the previous intensity.
function kindle.toggle_backlight()
	local intensity = kindle.get_backlight()
	if intensity and intensity > 0 then
		kindle.last_intensity = intensity
		kindle.set_backlight(0)
	else
		kindle.set_backlight(kindle.last_intensity or 10)
	end
end

events.connect(events.INITIALIZED, function()
	local menu = textadept.menu.menubar
	if menu then
		table.insert(menu, {
			title = _L['Kindle'],
			{_L['Toggle Backlight'], kindle.toggle_backlight},
			{_L['Backlight Up'], function() kindle.set_backlight(math.min((kindle.get_backlight() or 0) + 2, 24)) end},
			{_L['Backlight Down'], function() kindle.set_backlight(math.max((kindle.get_backlight() or 0) - 2, 0)) end},
			{''},
			{_L['Disable Sleep'], function() kindle.set_sleep(false) end},
			{_L['Enable Sleep'], function() kindle.set_sleep(true) end}
		})
	end
end)
