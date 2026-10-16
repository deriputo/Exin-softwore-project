"""Native startup DPI rendering; no Windows display settings are changed."""
import sys


def enable_native_dpi():
    """Call before Tk(). System-aware avoids bitmap upscaling on the startup DPI.

    Tk 8.6 does not reliably rescale all custom geometry between mixed-DPI
    monitors. We deliberately use system awareness rather than promising PMv2.
    """
    if sys.platform != "win32":
        return
    import ctypes
    try:
        set_context = ctypes.windll.user32.SetProcessDpiAwarenessContext
        set_context.argtypes = [ctypes.c_void_p]
        set_context.restype = ctypes.c_bool
        if set_context(ctypes.c_void_p(-2)):  # SYSTEM_AWARE
            return
    except (AttributeError, OSError):
        pass
    try:
        # Fallback for older Windows; may return false if the host set it already.
        ctypes.windll.user32.SetProcessDPIAware()
    except (AttributeError, OSError):
        pass


def dpi_scale(root):
    return max(1.0, min(4.0, float(root.winfo_fpixels("1i")) / 96.0))


def scale_pixels(root, scale):
    """Scale pixel geometry once; fonts and character-sized fields scale via Tk."""
    if abs(scale - 1.0) < 0.02:
        return

    def scaled(value):
        parts = root.tk.splitlist(value) if isinstance(value, str) else value
        if not isinstance(parts, (tuple, list)):
            parts = (parts,)
        return tuple(round(float(str(part)) * scale) for part in parts)

    def visit(parent):
        for widget in parent.winfo_children():
            cls = widget.winfo_class()
            if cls in ("Frame", "TFrame", "Canvas"):
                for option in ("width", "height"):
                    value = float(str(widget.cget(option)))
                    if value > 0:
                        widget.configure(**{option: round(value * scale)})
            if "padding" in widget.keys():
                value = widget.cget("padding")
                if value:
                    widget.configure(padding=scaled(value))
            if cls == "Text":
                for option in ("padx", "pady"):
                    widget.configure(**{option: round(float(str(widget.cget(option))) * scale)})
            manager = widget.winfo_manager()
            if manager in ("grid", "pack"):
                info = widget.grid_info() if manager == "grid" else widget.pack_info()
                configure = widget.grid_configure if manager == "grid" else widget.pack_configure
                for option in ("padx", "pady", "ipadx", "ipady"):
                    if option in info:
                        configure(**{option: scaled(info[option])})
            # Hidden BLE panel still needs its children's pixel geometry scaled.
            visit(widget)
    visit(root)
