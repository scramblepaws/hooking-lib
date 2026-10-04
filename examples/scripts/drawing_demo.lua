-- Drawing API demo: ESP-style primitives over the game.
-- Not loaded automatically; copy into <dll dir>/scripts/ to try it.
-- With real game data you would drive Position/From/To from entity coordinates.

local box = Drawing.new("Square")
box.Visible = true
box.Position = Vector2.new(560, 250)
box.Size = Vector2.new(160, 220)
box.Thickness = 2
box.Color = Color3.fromRGB(255, 70, 70)

local head = Drawing.new("Circle")
head.Visible = true
head.Position = Vector2.new(640, 240)
head.Radius = 10
head.Filled = true
head.Color = Color3.fromRGB(255, 70, 70)

local label = Drawing.new("Text")
label.Visible = true
label.Position = Vector2.new(640, 200)
label.Center = true
label.Size = 18
label.Outline = true
label.Text = "target"
label.Color = Color3.fromRGB(255, 255, 255)

local crosshair = Drawing.new("Line")
crosshair.Visible = true
crosshair.From = Vector2.new(620, 360)
crosshair.To = Vector2.new(660, 360)
crosshair.Thickness = 1
crosshair.Transparency = 0.3
crosshair.Color = Color3.fromRGB(80, 200, 255)

ui.event("drawing demo loaded")
