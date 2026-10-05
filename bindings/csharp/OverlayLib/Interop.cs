using System.Runtime.InteropServices;

namespace OverlayLib;

[StructLayout(LayoutKind.Sequential)]
public struct Vec2
{
    public float X;
    public float Y;

    public Vec2(float x, float y)
    {
        X = x;
        Y = y;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct Rect
{
    public float X;
    public float Y;
    public float W;
    public float H;

    public Rect(float x, float y, float w, float h)
    {
        X = x;
        Y = y;
        W = w;
        H = h;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct Color
{
    public float R;
    public float G;
    public float B;
    public float A;

    public Color(float r, float g, float b, float a = 1.0f)
    {
        R = r;
        G = g;
        B = b;
        A = a;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct InitInfo
{
    public uint Size;
    public Backend Backend;
    public uint Flags;
    public IntPtr TargetHwnd;

    [MarshalAs(UnmanagedType.LPUTF8Str)]
    public string? MenuTitle;
}

public enum Status
{
    Ok = 0,
    AlreadyInitialized = 1,
    NotInitialized = 2,
    HookFailed = 3,
    BackendUnavailable = 4,
    InvalidArgument = 5,
    OutOfMemory = 6,
    Io = 7,
    NotSupported = 8,
    Internal = 9,
}

public enum Backend
{
    Auto = 0,
    Dx11 = 1,
    Dx12 = 2,
    Vulkan = 3,
}

public enum WidgetKind
{
    Toggle = 0,
    Checkbox = 1,
    Slider = 2,
    Dropdown = 3,
    Keybind = 4,
    TextInput = 5,
    ColorPicker = 6,
    Button = 7,
}

public enum ToastPosition
{
    TopRight = 0,
    TopLeft = 1,
    BottomRight = 2,
    BottomLeft = 3,
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void PaintFn(IntPtr user, IntPtr widget, IntPtr canvas);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void ChromePaintFn(IntPtr user, IntPtr canvas, IntPtr menu);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void ConfigListFn(IntPtr user, IntPtr name);

internal static class Native
{
    internal const string Dll = "overlay.dll";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_abi_version")]
    internal static extern uint AbiVersion();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_version_string")]
    internal static extern IntPtr VersionString();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_init")]
    internal static extern Status Init(in InitInfo info);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_shutdown")]
    internal static extern Status Shutdown();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_is_ready")]
    internal static extern int IsReady();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_set_menu_open")]
    internal static extern int SetMenuOpen(int open);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_is_menu_open")]
    internal static extern int IsMenuOpen();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_context")]
    internal static extern IntPtr Context();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu")]
    internal static extern IntPtr Menu();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu_set_chrome_painter")]
    internal static extern void MenuSetChromePainter(IntPtr menu, ChromePaintFn painter, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu_rect")]
    internal static extern Rect MenuRect(IntPtr menu);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu_set_rect")]
    internal static extern void MenuSetRect(IntPtr menu, Rect rect);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu_set_page")]
    internal static extern void MenuSetPage(IntPtr menu, int index);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu_page_index")]
    internal static extern int MenuPageIndex(IntPtr menu);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu_page_count")]
    internal static extern nuint MenuPageCount(IntPtr menu);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_menu_page")]
    internal static extern IntPtr MenuPage(IntPtr menu, nuint index);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_page_create")]
    internal static extern IntPtr PageCreate(IntPtr context, [MarshalAs(UnmanagedType.LPUTF8Str)] string title,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? navSection);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_page_destroy")]
    internal static extern void PageDestroy(IntPtr page);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_page_title")]
    internal static extern IntPtr PageTitle(IntPtr page);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_page_nav_section")]
    internal static extern IntPtr PageNavSection(IntPtr page);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_page_section_count")]
    internal static extern nuint PageSectionCount(IntPtr page);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_page_section")]
    internal static extern IntPtr PageSection(IntPtr page, nuint index);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_section_create")]
    internal static extern IntPtr SectionCreate(IntPtr page, [MarshalAs(UnmanagedType.LPUTF8Str)] string title);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_section_destroy")]
    internal static extern void SectionDestroy(IntPtr section);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_section_title")]
    internal static extern IntPtr SectionTitle(IntPtr section);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_section_widget_count")]
    internal static extern nuint SectionWidgetCount(IntPtr section);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_section_widget")]
    internal static extern IntPtr SectionWidget(IntPtr section, nuint index);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_create")]
    internal static extern IntPtr WidgetCreate(IntPtr section, WidgetKind kind,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string id, [MarshalAs(UnmanagedType.LPUTF8Str)] string label);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_destroy")]
    internal static extern void WidgetDestroy(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_rect")]
    internal static extern void WidgetSetRect(IntPtr widget, Rect rect);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_bool")]
    internal static extern void WidgetSetBool(IntPtr widget, int value);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_get_bool")]
    internal static extern int WidgetGetBool(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_float")]
    internal static extern void WidgetSetFloat(IntPtr widget, float value);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_get_float")]
    internal static extern float WidgetGetFloat(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_int")]
    internal static extern void WidgetSetInt(IntPtr widget, int value);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_get_int")]
    internal static extern int WidgetGetInt(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_get_text")]
    internal static extern nuint WidgetGetText(IntPtr widget, IntPtr buffer, nuint capacity);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_text")]
    internal static extern void WidgetSetText(IntPtr widget, [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_key")]
    internal static extern void WidgetSetKey(IntPtr widget, int code);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_get_key")]
    internal static extern int WidgetGetKey(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_color")]
    internal static extern void WidgetSetColor(IntPtr widget, Color color);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_get_color")]
    internal static extern Color WidgetGetColor(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_range")]
    internal static extern void WidgetSetRange(IntPtr widget, float min, float max);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_format")]
    internal static extern void WidgetSetFormat(IntPtr widget, [MarshalAs(UnmanagedType.LPUTF8Str)] string format,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? altText);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_items")]
    internal static extern void WidgetSetItems(IntPtr widget, IntPtr items, nuint count);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_enabled")]
    internal static extern void WidgetSetEnabled(IntPtr widget, int enabled);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_set_painter")]
    internal static extern void WidgetSetPainter(IntPtr widget, PaintFn painter, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_rect")]
    internal static extern Rect WidgetRect(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_control_rect")]
    internal static extern Rect WidgetControlRect(IntPtr widget, float scale);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_is_hovered")]
    internal static extern int WidgetIsHovered(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_is_focused")]
    internal static extern int WidgetIsFocused(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_was_clicked")]
    internal static extern int WidgetWasClicked(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_widget_anim")]
    internal static extern float WidgetAnim(IntPtr widget);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_scale")]
    internal static extern float CanvasScale(IntPtr canvas);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_measure_text")]
    internal static extern Vec2 CanvasMeasureText(IntPtr canvas, [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_measure_text_sized")]
    internal static extern Vec2 CanvasMeasureTextSized(IntPtr canvas, [MarshalAs(UnmanagedType.LPUTF8Str)] string text,
        float size);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_fill_rect")]
    internal static extern void CanvasFillRect(IntPtr canvas, Rect rect, Color color, float radius);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_stroke_rect")]
    internal static extern void CanvasStrokeRect(IntPtr canvas, Rect rect, Color color, float thickness, float radius);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_fill_circle")]
    internal static extern void CanvasFillCircle(IntPtr canvas, Vec2 center, float radius, Color color);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_stroke_circle")]
    internal static extern void CanvasStrokeCircle(IntPtr canvas, Vec2 center, float radius, Color color,
        float thickness);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_fill_triangle")]
    internal static extern void CanvasFillTriangle(IntPtr canvas, Vec2 a, Vec2 b, Vec2 c, Color color);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_text")]
    internal static extern void CanvasText(IntPtr canvas, Vec2 pos, Color color,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_text_sized")]
    internal static extern void CanvasTextSized(IntPtr canvas, Vec2 pos, Color color, float size,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_text_centered")]
    internal static extern void CanvasTextCentered(IntPtr canvas, Rect rect, Color color,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_push_clip")]
    internal static extern void CanvasPushClip(IntPtr canvas, Rect rect);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_canvas_pop_clip")]
    internal static extern void CanvasPopClip(IntPtr canvas);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_config_save")]
    internal static extern Status ConfigSave([MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_config_load")]
    internal static extern Status ConfigLoad([MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_config_delete")]
    internal static extern Status ConfigDelete([MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_config_list")]
    internal static extern void ConfigList(ConfigListFn callback, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_notify")]
    internal static extern void Notify([MarshalAs(UnmanagedType.LPUTF8Str)] string message);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_event")]
    internal static extern void Event([MarshalAs(UnmanagedType.LPUTF8Str)] string message);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_toast_position")]
    internal static extern void ToastPosition(ToastPosition position);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_toast_duration")]
    internal static extern void ToastDuration(float seconds);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_style_parse")]
    internal static extern Status StyleParse([MarshalAs(UnmanagedType.LPUTF8Str)] string css);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_style_load")]
    internal static extern Status StyleLoad([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_style_clear")]
    internal static extern void StyleClear();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_script_execute")]
    internal static extern Status ScriptExecute([MarshalAs(UnmanagedType.LPUTF8Str)] string source,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? chunkName);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "overlay_script_reload")]
    internal static extern Status ScriptReload();
}
