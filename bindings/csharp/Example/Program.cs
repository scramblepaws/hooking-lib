using OverlayLib;

var failures = 0;
void Check(bool condition, string what)
{
    Console.WriteLine($"{(condition ? "ok  " : "FAIL")} {what}");
    if (!condition) ++failures;
}

Check(Overlay.AbiVersion == 0, $"abi version ({Overlay.AbiVersion})");
Check(!string.IsNullOrEmpty(Overlay.VersionString), $"version string ({Overlay.VersionString})");

var status = Overlay.Init(Backend.Auto, Overlay.FlagExternal, IntPtr.Zero, "C# host");
Check(status == Status.Ok, $"init ({status})");
if (status != Status.Ok) return 1;

Check(Overlay.Context.Handle != IntPtr.Zero, "context handle");
var menu = Overlay.Menu;
Check(menu.Handle != IntPtr.Zero, "menu handle");

var page = Overlay.Context.CreatePage("Host", "C#");
var section = page.CreateSection("Binding smoke");
var slider = section.CreateWidget(WidgetKind.Slider, "cs_slider", "Value");
var toggle = section.CreateWidget(WidgetKind.Toggle, "cs_toggle", "Enabled");
var text = section.CreateWidget(WidgetKind.TextInput, "cs_text", "Name");
var dropdown = section.CreateWidget(WidgetKind.Dropdown, "cs_dropdown", "Mode");
var color = section.CreateWidget(WidgetKind.ColorPicker, "cs_color", "Color");

Check(menu.PageCount >= 1, $"page registered ({menu.PageCount})");
Check(page.SectionCount == 1 && section.WidgetCount == 5, "menu model");

slider.SetRange(0.0f, 100.0f);
slider.SetFloat(41.5f);
toggle.SetBool(true);
text.SetText("hello");
dropdown.SetItems(new[] { "one", "two", "three" });
dropdown.SetInt(2);
color.SetColor(new Color(0.25f, 0.5f, 0.75f, 1.0f));

Check(Math.Abs(slider.GetFloat() - 41.5f) < 0.001f, "slider value");
Check(toggle.GetBool(), "toggle value");
Check(text.GetText() == "hello", "text value");
Check(dropdown.GetInt() == 2, "dropdown value");

var save = Overlay.ConfigSave("csharp_smoke");
slider.SetFloat(0.0f);
toggle.SetBool(false);
text.SetText("");
dropdown.SetInt(0);
color.SetColor(default);
var load = Overlay.ConfigLoad("csharp_smoke");
Check(save == Status.Ok && load == Status.Ok, $"config round trip ({save}, {load})");
Check(Math.Abs(slider.GetFloat() - 41.5f) < 0.001f, "slider restored");
Check(toggle.GetBool(), "toggle restored");
Check(text.GetText() == "hello", "text restored");
Check(dropdown.GetInt() == 2, "dropdown restored");
var restored = color.GetColor();
Check(Math.Abs(restored.R - 0.25f) < 0.001f && Math.Abs(restored.B - 0.75f) < 0.001f, "color restored");
Overlay.ConfigDelete("csharp_smoke");

Check(Overlay.StyleParse("menu { accent: #ff8800; rounding: 6; }") == Status.Ok, "stylesheet parse");
Overlay.StyleClear();
Overlay.Notify("C# binding");
Overlay.Event("C# binding event");

Check(Overlay.Shutdown() == Status.Ok, "shutdown");

Console.WriteLine(failures == 0 ? "PASS" : $"FAIL ({failures})");
return failures == 0 ? 0 : 1;
