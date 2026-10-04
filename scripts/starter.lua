-- Starter menu, scripted. Mirrors the built-in C++ starter one-to-one so the two
-- paths produce the same layout and the same widget ids (ids are config keys).

local ragebot = ui.page("Ragebot", "Aimbot")
local main = ui.section(ragebot, "MAIN")
ui.toggle(main, "enabled", "Enabled", false)
ui.toggle(main, "peek_assist", "Peek Assist", false)
ui.toggle(main, "hide_shots", "Hide Shots", false)
ui.toggle(main, "double_tap", "Double Tap", false)
ui.slider(main, "fov", "Field of View", 180, 0, 180, "%.0f°")
ui.checkbox(main, "visualize", "Visualize", false)
ui.keybind(main, "peak_key", "Peak Key", 0)

local accuracy = ui.section(ragebot, "ACCURACY")
ui.toggle(accuracy, "auto_scope", "Auto Scope", false)
ui.toggle(accuracy, "auto_stop", "Auto Stop", false)
ui.colorpicker(accuracy, "tracer_color", "Tracer Color", "#b23636")

local selection = ui.section(ragebot, "SELECTION")
local hitboxes = {"Head", "Stomach", "Head, Stomach", "Nearest", "Chest"}
ui.dropdown(selection, "hitboxes", "Hitboxes", hitboxes, 2)
ui.dropdown(selection, "multipoint", "Multipoint", hitboxes, 2)
ui.slider(selection, "hit_chance", "Hit Chance", 65, 0, 100, "%.0f%%")
ui.slider(selection, "min_damage", "Min. Damage", 0, 0, 100, "%.0f", "Auto")
ui.toggle(selection, "penetrate_walls", "Penetrate Walls", true)

local safety = ui.section(ragebot, "SAFETY")
ui.dropdown(safety, "body_aim", "Body Aim", {"Default", "Nearest", "Head", "Chest"}, 0)
ui.dropdown(safety, "safe_points", "Safe Points", {"Default", "Strict", "Aggressive"}, 0)
ui.dropdown(safety, "hitbox_safety", "Ensure Hitbox Safety", {"Select", "Always", "On Key"}, 0)

ui.page("Anti Aim", "Aimbot")
ui.page("Legitbot", "Aimbot")
ui.page("Players", "Visuals")
ui.page("World", "Visuals")
ui.page("Inventory", "Visuals")
ui.page("Main", "Miscellaneous")
ui.page("Scripts", "Miscellaneous")

local configs = ui.page("Configs", "Miscellaneous")
local config_section = ui.section(configs, "CONFIGS")
ui.text_input(config_section, "config_name", "Config Name", "default")
ui.button(config_section, "config_save", "Save")
ui.button(config_section, "config_load", "Load")

ui.event("menu loaded")
