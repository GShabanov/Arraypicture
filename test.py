import threading
import tkinter as tk
import arraypicture as ap

root = tk.Tk()
root.geometry("820x620")

def cell(color=0xFFFFFF, symbol=None, data=None):
    """Зручний конструктор для клітинки set_cells()."""
    return {"color": int(color) & 0xFFFFFF,
            "symbol": symbol if (symbol is None or isinstance(symbol, str)) else str(symbol),
            "data":   None if data is None else int(data)}


frame = tk.Frame(root, width=800, height=600, bg="#303030")
frame.place(x=10, y=10)

hwnd = frame.winfo_id()  # HWND parent

#input("Attach debugger and press Enter to continue...")

pic = ap.ArrayPicture()
pic.create(hwnd, 0, 0, 800, 600, cx=64, cy=64, markerSize=2, granularity = 24)
pic.set_granularity(16)

def on_pt(x, y):
    print("paint:", x, y)

pic.set_on_point(on_pt)

# white
mat = []

for i in range(64):

    row = []
    for j in range(64):
        row.append(cell())

    mat.append(row)


        
#mat = [[0xFFFFFF for _ in range(128)] for _ in range(96)]
pic.set_input(mat)

root.mainloop()
