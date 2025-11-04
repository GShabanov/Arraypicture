import tkinter as tk
import arraypicture as ap

def cell(color=0xFFFFFF, symbol=None, data=None):
    return {"color": int(color) & 0xFFFFFF,
            "symbol": symbol if (symbol is None or isinstance(symbol, str)) else str(symbol),
            "data":   None if data is None else int(data)}

def make_labyrinth(w=24, h=24):
    grid = [[cell(0xFFFFFF, " ", None) for _ in range(w)] for _ in range(h)]

    # рамка
    for x in range(w):
        grid[0][x] = cell(0x303030, "█")
        grid[h - 1][x] = cell(0x303030, "█")
    for y in range(h):
        grid[y][0] = cell(0x303030, "█")
        grid[y][w - 1] = cell(0x303030, "█")

    # всередині зробимо простий лабіринт (стіни через одну)
    for y in range(2, h - 2, 2):
        for x in range(2, w - 2, 2):
            grid[y][x] = cell(0x707070, "█")
            if x + 1 < w - 1:
                grid[y][x + 1] = cell(0x707070, "█")

    # вхід/вихід
    grid[1][1] = cell(0x00FF00, "A")   # вхід (зелений)
    grid[h - 2][w - 2] = cell(0xFF0000, "Б")  # вихід (червоний)

    # кілька орієнтирів усередині
    letters = ["В", "Г", "Д", "Е", "Є", "Ж", "З", "И", "І", "Ї", "К", "Л", "М", "Н", "О"]
    px, py = 3, 3
    for ch in letters:
        if py < h - 2 and px < w - 2:
            grid[py][px] = cell(0xCCCCFF, ch)
            px += 2
            if px >= w - 3:
                px = 3
                py += 2
    return grid


# ---------- tkinter ----------
root = tk.Tk()
root.title("Лабіринт 24×24 (arraypicture)")
root.geometry("700x700")

holder = tk.Frame(root, width=602, height=602, bg="#202020")
holder.place(x=20, y=20)

hwnd = holder.winfo_id()

pic = ap.ArrayPicture()
#             x  y   w    h   cx   cy  marker
pic.create(hwnd, 0,  0,  602, 602,  24, 24,  1, 24)

# створюємо та показуємо лабіринт
grid = make_labyrinth(24, 24)
pic.set_input(grid)

# колбек при кліку — фарбує клітинку
def on_point(x, y):
    lin = pic.serialize_cells()
    idx = y * 24 + x
    c = lin[idx]
    c["color"] = 0xFFFF66
    c["symbol"] = "·"
    lin[idx] = c
    mat = [lin[i * 24:(i + 1) * 24] for i in range(24)]
    pic.set_input(mat)
    root.title(f"Clicked: ({x},{y})")

pic.set_on_point(on_point)

root.mainloop()
