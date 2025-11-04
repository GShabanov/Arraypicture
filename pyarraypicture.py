# arraypicture.py  — pure Python fallback for ArrayPicture (tkinter)
from __future__ import annotations
import tkinter as tk
from typing import Callable, Optional, List, Dict, Any

# Тип клітинки: {"color": int #RRGGBB, "symbol": Optional[str], "data": Optional[int]}
Cell = Dict[str, Any]

def _rgb_int_to_hex(rgb: int) -> str:
    rgb &= 0xFFFFFF
    return f"#{(rgb >> 16) & 0xFF:02x}{(rgb >> 8) & 0xFF:02x}{rgb & 0xFF:02x}"

def _find_widget_by_hwnd(root: tk.Misc, hwnd: int) -> Optional[tk.Misc]:
    # Пробігаємо все дерево tk та шукаємо widget з таким же winfo_id()
    stack = [root]
    while stack:
        w = stack.pop()
        try:
            if int(w.winfo_id()) == int(hwnd):
                return w
        except Exception:
            pass
        try:
            stack.extend(w.winfo_children())
        except Exception:
            pass
    return None

class ArrayPicture:
    """ArrayPicture WinAPI-compatible (pure Python tkinter backend)."""

    # -------- життєвий цикл --------
    def __init__(self):
        self._canvas: Optional[tk.Canvas] = None
        self._parent: Optional[tk.Misc] = None
        self._cx = 0
        self._cy = 0
        self._marker = 1
        self._gran = 6  # клітинка = gran + 1 пікселів
        self._on_point: Optional[Callable[[int, int], None]] = None
        self._grid: List[List[Cell]] = []  # [y][x]
        self._width_px = 0
        self._height_px = 0
        self._cell_cache: List[List[Dict[str, int]]] = []  # ids {rect, text}

    # API: create(parent_hwnd, x, y, w, h, cx=64, cy=64, markerSize=1, granularity=6)
    def create(self, parent_hwnd: int, x: int, y: int, w: int, h: int,
               cx: int = 64, cy: int = 64, markerSize: int = 1, granularity: int = 6) -> bool:
        root = tk._default_root
        if root is None:
            raise RuntimeError("tkinter root is not initialized (create Tk() first)")

        parent = _find_widget_by_hwnd(root, parent_hwnd)
        if parent is None:
            # якщо не знайшли — ставимо на root з тим самим позиціонуванням
            parent = root

        self._parent = parent
        self._cx = max(1, int(cx))
        self._cy = max(1, int(cy))
        self._marker = max(1, int(markerSize))
        self._gran = max(0, int(granularity))

        self._canvas = tk.Canvas(parent, bd=0, highlightthickness=0, bg="#202020")
        # позиціонування усередині parent як у WinAPI create(x,y,w,h)
        try:
            self._canvas.place(x=x, y=y, width=w, height=h)
        except Exception:
            # якщо parent — root, все одно працює
            self._canvas.place(x=x, y=y, width=w, height=h)

        self._width_px = int(w)
        self._height_px = int(h)

        # дані за замовчуванням
        self._grid = [[{"color": 0xFFFFFF, "symbol": None, "data": None}
                       for _ in range(self._cx)] for __ in range(self._cy)]

        # підписки
        self._canvas.bind("<Configure>", self._on_resize)
        self._canvas.bind("<ButtonPress-1>", self._on_lmb_down)
        self._canvas.bind("<B1-Motion>", self._on_lmb_drag)
        self._canvas.bind("<ButtonRelease-1>", self._on_lmb_up)

        self._redraw_all()
        return True

    def destroy(self):
        if self._canvas is not None:
            try:
                self._canvas.destroy()
            except Exception:
                pass
        self._canvas = None
        self._parent = None
        self._grid = []

    # -------- властивості / getset --------
    @property
    def hwnd(self) -> int:
        if self._canvas is None:
            return 0
        try:
            return int(self._canvas.winfo_id())
        except Exception:
            return 0

    @property
    def width(self) -> int:
        return self._cx

    @property
    def height(self) -> int:
        return self._cy

    # -------- публічний API, сумісний з C++ модулем --------
    def set_granularity(self, gran: int) -> None:
        self._gran = max(0, int(gran))
        self._redraw_all()

    def clear(self) -> None:
        for y in range(self._cy):
            for x in range(self._cx):
                c = self._grid[y][x]
                c["color"] = 0xFFFFFF
                c["symbol"] = None
                c["data"] = None
        self._redraw_all()

    # set_input(matrix): list[list[dict(color,symbol,data)]]
    def set_input(self, matrix: List[List[Cell]]) -> None:
        if not matrix or not isinstance(matrix, list) or not isinstance(matrix[0], list):
            raise TypeError("set_input expects list[list[dict]]")
        h = len(matrix)
        w = len(matrix[0])
        # переналаштовуємо розмір, якщо змінився
        self._cy, self._cx = h, w
        self._grid = [[{"color": 0xFFFFFF, "symbol": None, "data": None}
                       for _ in range(self._cx)] for __ in range(self._cy)]

        for y in range(h):
            row = matrix[y]
            if len(row) != w:
                raise ValueError("matrix rows must be same length")
            for x in range(w):
                it = row[x]
                if not isinstance(it, dict):
                    raise TypeError("cell must be dict")
                rgb = int(it.get("color", 0xFFFFFF)) & 0xFFFFFF
                sym = it.get("symbol", None)
                if sym is not None:
                    sym = str(sym)
                    # беремо лише 1-й кодпоінт (як у нативній версії)
                    sym = sym[:1]
                data = it.get("data", None)
                self._grid[y][x] = {"color": rgb, "symbol": sym, "data": None if data is None else int(data)}
        self._redraw_all()

    # serialize_cells(): row-major list[dict]
    def serialize_cells(self) -> List[Cell]:
        out: List[Cell] = []
        for y in range(self._cy):
            for x in range(self._cx):
                c = self._grid[y][x]
                out.append({"color": int(c["color"]) & 0xFFFFFF,
                            "symbol": c["symbol"] if c["symbol"] is None else str(c["symbol"])[:1],
                            "data": c["data"]})
        return out

    # serialize_rgb(): row-major list[int]
    def serialize_rgb(self) -> List[int]:
        out: List[int] = []
        for y in range(self._cy):
            for x in range(self._cx):
                out.append(int(self._grid[y][x]["color"]) & 0xFFFFFF)
        return out

    # set_on_point(callback or None)
    def set_on_point(self, cb: Optional[Callable[[int, int], None]]) -> None:
        if cb is not None and not callable(cb):
            raise TypeError("callback must be callable or None")
        self._on_point = cb

    # -------- внутрішнє малювання --------
    def _cell_px(self) -> int:
        return self._gran + 1

    def _on_resize(self, ev):
        self._width_px = int(ev.width)
        self._height_px = int(ev.height)
        self._redraw_all()

    def _redraw_all(self):
        if self._canvas is None:
            return
        self._canvas.delete("all")
        step = self._cell_px()
        wpx = self._cx * step + 1
        hpx = self._cy * step + 1
        # фон
        self._canvas.create_rectangle(0, 0, wpx, hpx, outline="", fill="#202020")

        # клітини
        self._cell_cache = [[{"rect": 0, "text": 0} for _ in range(self._cx)] for __ in range(self._cy)]
        for y in range(self._cy):
            for x in range(self._cx):
                x0 = x * step + 1
                y0 = y * step + 1
                x1 = x0 + step
                y1 = y0 + step
                c = self._grid[y][x]
                rid = self._canvas.create_rectangle(
                    x0, y0, x1, y1, outline="", fill=_rgb_int_to_hex(int(c["color"]))
                )
                tid = 0
                if c["symbol"]:
                    tid = self._canvas.create_text(
                        (x0 + x1) // 2, (y0 + y1) // 2,
                        text=str(c["symbol"])[:1],
                        fill="#000000",
                        anchor="center"
                    )
                self._cell_cache[y][x]["rect"] = rid
                self._cell_cache[y][x]["text"] = tid

        # сітка
        grid_color = "#505050"
        for xx in range(self._cx + 1):
            X = xx * step
            self._canvas.create_line(X, 0, X, self._cy * step, fill=grid_color)
        for yy in range(self._cy + 1):
            Y = yy * step
            self._canvas.create_line(0, Y, self._cx * step, Y, fill=grid_color)

    def _update_cell(self, x: int, y: int):
        if self._canvas is None:
            return
        if not (0 <= x < self._cx and 0 <= y < self._cy):
            return
        step = self._cell_px()
        x0 = x * step + 1
        y0 = y * step + 1
        x1 = x0 + step
        y1 = y0 + step
        c = self._grid[y][x]
        ids = self._cell_cache[y][x]
        # фон
        if ids["rect"]:
            self._canvas.itemconfig(ids["rect"], fill=_rgb_int_to_hex(int(c["color"])))
        else:
            ids["rect"] = self._canvas.create_rectangle(x0, y0, x1, y1, outline="", fill=_rgb_int_to_hex(int(c["color"])))
        # текст
        if ids["text"]:
            if c["symbol"]:
                self._canvas.itemconfig(ids["text"], text=str(c["symbol"])[:1])
                self._canvas.coords(ids["text"], (x0 + x1) // 2, (y0 + y1) // 2)
            else:
                self._canvas.delete(ids["text"])
                ids["text"] = 0
        else:
            if c["symbol"]:
                ids["text"] = self._canvas.create_text(
                    (x0 + x1) // 2, (y0 + y1) // 2,
                    text=str(c["symbol"])[:1],
                    fill="#000000",
                    anchor="center"
                )

    # -------- події миші --------
    def _event_to_cell(self, ev) -> tuple[int, int]:
        step = self._cell_px()
        x = ev.x // step
        y = ev.y // step
        return int(x), int(y)

    def _on_lmb_down(self, ev):
        x, y = self._event_to_cell(ev)
        self._paint_mark(x, y)

    def _on_lmb_drag(self, ev):
        x, y = self._event_to_cell(ev)
        self._paint_mark(x, y)

    def _on_lmb_up(self, ev):
        # нічого
        pass

    def _paint_mark(self, x: int, y: int):
        if x < 0 or y < 0 or x >= self._cx or y >= self._cy:
            return
        # Залишаємо сумісність: маркер фарбує чорним, символ не чіпаємо
        def setblack(ix, iy):
            if 0 <= ix < self._cx and 0 <= iy < self._cy:
                self._grid[iy][ix]["color"] = 0x000000
                self._update_cell(ix, iy)

        setblack(x, y)
        if self._marker > 1:
            setblack(x - 1, y); setblack(x + 1, y); setblack(x, y - 1); setblack(x, y + 1)
            if self._marker > 2:
                setblack(x - 1, y - 1); setblack(x + 1, y - 1)
                setblack(x - 1, y + 1); setblack(x + 1, y + 1)

        if self._on_point:
            try:
                self._on_point(x, y)
            except Exception:
                # як у нативній версії — не валимо UI
                pass
