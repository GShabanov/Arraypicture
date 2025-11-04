import tkinter as tk
import arraypicture as ap

# ---------- helpers ----------
def cell(color=0xFFFFFF, symbol=None, data=None):
    """Зручний конструктор для клітинки set_cells()."""
    return {"color": int(color) & 0xFFFFFF,
            "symbol": symbol if (symbol is None or isinstance(symbol, str)) else str(symbol),
            "data":   None if data is None else int(data)}

def make_cells(w, h, fill=0xFFFFFF):
    return [[cell(fill, None, None) for _ in range(w)] for __ in range(h)]


# ---------- tkinter ----------
root = tk.Tk()
root.title("arraypicture abi3 demo")
root.geometry("1024x768")

holder = tk.Frame(root, width=602, height=602, bg="#202020")
holder.place(x=20, y=20)

# HWND батька (tkinter Frame)
hwnd = holder.winfo_id()

# ---------- arraypicture control ----------
pic = ap.ArrayPicture()
#               x  y   w    h   cx   cy  marker
pic.create(hwnd, 0,  0,  602, 602,  24, 24,  1, 24)

# записуємо символи у декілька клітин
grid = make_cells(24, 24, fill=0xFFFFCC)
grid[3][4]["symbol"] = "A"
grid[4][4]["symbol"] = "Б"
grid[5][4]["symbol"] = "В"
grid[6][4]["symbol"] = "Г"
grid[7][4]["symbol"] = "Д"
# Цвет можно менять точечно:
grid[5][4]["color"] = 0xFFDDDD
pic.set_input(grid)

# Коллбек на клітинку : ставим символ и меняем цвет
palette = ["А", "Б", "В", "Г", "Д", "І", "Ї", "К", "γ", None]  # None = немає символу
pal_index = 0

def on_point(x, y):
    # читаємо стан матриці 
    lin = pic.serialize_cells()   # row-major: довжина = width*height
    w = pic.width
    h = pic.height
    idx = y * w + x
    cell_dict = lin[idx]

    # замінимо колір / символ
    global pal_index
    pal_index = (pal_index + 1) % len(palette)
    sym = palette[pal_index]
    new_color = 0xCCFFCC if sym else 0xFFFFCC

    # локально змінюємо і записуємо в зворотньому напрямку (демонстрація).
    cell_dict["color"] = new_color
    cell_dict["symbol"] = sym
    lin[idx] = cell_dict

    # зберемо знову у матрицю і обновимо контрол
    mat = [lin[i * w:(i + 1) * w] for i in range(h)]
    pic.set_input(mat)

    root.title(f"clicked ({x},{y}) → symbol={sym!r}")

pic.set_on_point(on_point)

root.mainloop()