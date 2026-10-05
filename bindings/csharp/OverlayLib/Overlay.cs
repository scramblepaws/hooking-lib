using System.Runtime.InteropServices;

namespace OverlayLib;

public static class Overlay
{
    public const uint FlagExternal = 0x00000001u;

    public static uint AbiVersion => Native.AbiVersion();

    public static string VersionString => Marshal.PtrToStringUTF8(Native.VersionString()) ?? string.Empty;

    public static Status Init(Backend backend = Backend.Auto, uint flags = 0, IntPtr targetHwnd = default,
        string? menuTitle = null)
    {
        var info = new InitInfo
        {
            Size = (uint)Marshal.SizeOf<InitInfo>(),
            Backend = backend,
            Flags = flags,
            TargetHwnd = targetHwnd,
            MenuTitle = menuTitle,
        };
        return Native.Init(in info);
    }

    public static Status Shutdown() => Native.Shutdown();

    public static bool IsReady => Native.IsReady() != 0;

    public static bool MenuOpen
    {
        get => Native.IsMenuOpen() != 0;
        set => Native.SetMenuOpen(value ? 1 : 0);
    }

    public static Context Context => new(Native.Context());

    public static Menu Menu => new(Native.Menu());

    public static Status ConfigSave(string name) => Native.ConfigSave(name);

    public static Status ConfigLoad(string name) => Native.ConfigLoad(name);

    public static Status ConfigDelete(string name) => Native.ConfigDelete(name);

    public static void ConfigList(Action<string> callback)
    {
        ConfigListFn forward = (_, name) => callback(Marshal.PtrToStringUTF8(name) ?? string.Empty);
        Native.ConfigList(forward, IntPtr.Zero);
    }

    public static void Notify(string message) => Native.Notify(message);

    public static void Event(string message) => Native.Event(message);

    public static void ToastPosition(ToastPosition position) => Native.ToastPosition(position);

    public static void ToastDuration(float seconds) => Native.ToastDuration(seconds);

    public static Status StyleParse(string css) => Native.StyleParse(css);

    public static Status StyleLoad(string path) => Native.StyleLoad(path);

    public static void StyleClear() => Native.StyleClear();

    public static Status ScriptExecute(string source, string? chunkName = null) =>
        Native.ScriptExecute(source, chunkName);

    public static Status ScriptReload() => Native.ScriptReload();
}

public readonly struct Context
{
    public readonly IntPtr Handle;

    internal Context(IntPtr handle) => Handle = handle;

    public bool IsNull => Handle == IntPtr.Zero;

    public Page CreatePage(string title, string? navSection = null) => new(Native.PageCreate(Handle, title, navSection));
}

public readonly struct Menu
{
    public readonly IntPtr Handle;

    internal Menu(IntPtr handle) => Handle = handle;

    public bool IsNull => Handle == IntPtr.Zero;

    public Rect Rect => Native.MenuRect(Handle);

    public void SetRect(Rect rect) => Native.MenuSetRect(Handle, rect);

    public void SetChromePainter(ChromePaintFn painter, IntPtr user = default) =>
        Native.MenuSetChromePainter(Handle, painter, user);

    public int PageIndex => Native.MenuPageIndex(Handle);

    public void SetPage(int index) => Native.MenuSetPage(Handle, index);

    public nuint PageCount => Native.MenuPageCount(Handle);

    public Page GetPage(nuint index) => new(Native.MenuPage(Handle, index));
}

public readonly struct Page
{
    public readonly IntPtr Handle;

    internal Page(IntPtr handle) => Handle = handle;

    public bool IsNull => Handle == IntPtr.Zero;

    public string Title => Marshal.PtrToStringUTF8(Native.PageTitle(Handle)) ?? string.Empty;

    public string NavSection => Marshal.PtrToStringUTF8(Native.PageNavSection(Handle)) ?? string.Empty;

    public nuint SectionCount => Native.PageSectionCount(Handle);

    public Section GetSection(nuint index) => new(Native.PageSection(Handle, index));

    public Section CreateSection(string title) => new(Native.SectionCreate(Handle, title));

    public void Destroy() => Native.PageDestroy(Handle);
}

public readonly struct Section
{
    public readonly IntPtr Handle;

    internal Section(IntPtr handle) => Handle = handle;

    public bool IsNull => Handle == IntPtr.Zero;

    public string Title => Marshal.PtrToStringUTF8(Native.SectionTitle(Handle)) ?? string.Empty;

    public nuint WidgetCount => Native.SectionWidgetCount(Handle);

    public Widget GetWidget(nuint index) => new(Native.SectionWidget(Handle, index));

    public Widget CreateWidget(WidgetKind kind, string id, string label) =>
        new(Native.WidgetCreate(Handle, kind, id, label));

    public void Destroy() => Native.SectionDestroy(Handle);
}

public readonly struct Widget
{
    public readonly IntPtr Handle;

    internal Widget(IntPtr handle) => Handle = handle;

    public bool IsNull => Handle == IntPtr.Zero;

    public void SetRect(Rect rect) => Native.WidgetSetRect(Handle, rect);

    public bool GetBool() => Native.WidgetGetBool(Handle) != 0;

    public void SetBool(bool value) => Native.WidgetSetBool(Handle, value ? 1 : 0);

    public float GetFloat() => Native.WidgetGetFloat(Handle);

    public void SetFloat(float value) => Native.WidgetSetFloat(Handle, value);

    public int GetInt() => Native.WidgetGetInt(Handle);

    public void SetInt(int value) => Native.WidgetSetInt(Handle, value);

    public string GetText()
    {
        var required = Native.WidgetGetText(Handle, IntPtr.Zero, 0);
        if (required <= 1) return string.Empty;
        var buffer = Marshal.AllocHGlobal((int)required);
        try
        {
            Native.WidgetGetText(Handle, buffer, required);
            return Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    public void SetText(string value) => Native.WidgetSetText(Handle, value);

    public int GetKey() => Native.WidgetGetKey(Handle);

    public void SetKey(int code) => Native.WidgetSetKey(Handle, code);

    public Color GetColor() => Native.WidgetGetColor(Handle);

    public void SetColor(Color color) => Native.WidgetSetColor(Handle, color);

    public void SetRange(float min, float max) => Native.WidgetSetRange(Handle, min, max);

    public void SetFormat(string format, string? altText = null) => Native.WidgetSetFormat(Handle, format, altText);

    public void SetItems(IReadOnlyList<string> items)
    {
        if (items.Count == 0)
        {
            Native.WidgetSetItems(Handle, IntPtr.Zero, 0);
            return;
        }
        var strings = new IntPtr[items.Count];
        var array = IntPtr.Zero;
        try
        {
            for (var i = 0; i < items.Count; ++i) strings[i] = Marshal.StringToCoTaskMemUTF8(items[i]);
            array = Marshal.AllocHGlobal(IntPtr.Size * items.Count);
            Marshal.Copy(strings, 0, array, items.Count);
            Native.WidgetSetItems(Handle, array, (nuint)items.Count);
        }
        finally
        {
            if (array != IntPtr.Zero) Marshal.FreeHGlobal(array);
            foreach (var pointer in strings) Marshal.FreeCoTaskMem(pointer);
        }
    }

    public void SetEnabled(bool enabled) => Native.WidgetSetEnabled(Handle, enabled ? 1 : 0);

    public void SetPainter(PaintFn painter, IntPtr user = default) => Native.WidgetSetPainter(Handle, painter, user);

    public Rect Rect => Native.WidgetRect(Handle);

    public Rect ControlRect(float scale = 1.0f) => Native.WidgetControlRect(Handle, scale);

    public bool IsHovered => Native.WidgetIsHovered(Handle) != 0;

    public bool IsFocused => Native.WidgetIsFocused(Handle) != 0;

    public bool WasClicked => Native.WidgetWasClicked(Handle) != 0;

    public float Anim => Native.WidgetAnim(Handle);

    public void Destroy() => Native.WidgetDestroy(Handle);
}

public readonly struct Canvas
{
    public readonly IntPtr Handle;

    internal Canvas(IntPtr handle) => Handle = handle;

    public bool IsNull => Handle == IntPtr.Zero;

    public float Scale => Native.CanvasScale(Handle);

    public Vec2 MeasureText(string text) => Native.CanvasMeasureText(Handle, text);

    public Vec2 MeasureText(string text, float size) => Native.CanvasMeasureTextSized(Handle, text, size);

    public void FillRect(Rect rect, Color color, float radius = 0.0f) =>
        Native.CanvasFillRect(Handle, rect, color, radius);

    public void StrokeRect(Rect rect, Color color, float thickness = 1.0f, float radius = 0.0f) =>
        Native.CanvasStrokeRect(Handle, rect, color, thickness, radius);

    public void FillCircle(Vec2 center, float radius, Color color) =>
        Native.CanvasFillCircle(Handle, center, radius, color);

    public void StrokeCircle(Vec2 center, float radius, Color color, float thickness = 1.0f) =>
        Native.CanvasStrokeCircle(Handle, center, radius, color, thickness);

    public void FillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color) =>
        Native.CanvasFillTriangle(Handle, a, b, c, color);

    public void Text(Vec2 position, Color color, string text) => Native.CanvasText(Handle, position, color, text);

    public void Text(Vec2 position, Color color, float size, string text) =>
        Native.CanvasTextSized(Handle, position, color, size, text);

    public void TextCentered(Rect rect, Color color, string text) =>
        Native.CanvasTextCentered(Handle, rect, color, text);

    public void PushClip(Rect rect) => Native.CanvasPushClip(Handle, rect);

    public void PopClip() => Native.CanvasPopClip(Handle);
}
