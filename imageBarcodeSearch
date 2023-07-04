import tkinter as tk
from tkinter import filedialog, ttk
from pyzbar.pyzbar import decode
import sqlite3
import cv2

DB_NAME = 'barcodes.db'

def connect_to_db():
    conn = sqlite3.connect(DB_NAME)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS barcodes
                 (data text)''')
    return conn, c

def save_to_db(data):
    conn, c = connect_to_db()
    c.execute("INSERT INTO barcodes VALUES (?)", (data,))
    conn.commit()
    conn.close()

def load_from_db():
    conn, c = connect_to_db()
    c.execute("SELECT * FROM barcodes")
    data = c.fetchall()
    conn.close()
    return data

def view_all_db():
    conn, c = connect_to_db()
    c.execute("SELECT * FROM barcodes")
    all_rows = c.fetchall()
    conn.close()
    for row in all_rows:
        print(row)

view_all_db()

def remove_from_db(data):
    print(f"Data: {data}, Type: {type(data)}")
    conn, c = connect_to_db()
    c.execute("SELECT * FROM barcodes WHERE data LIKE ?", (data[0],))
    result = c.fetchone()
    if result:
        c.execute("DELETE FROM barcodes WHERE data LIKE ?", (data[0],))
        conn.commit()
        print(f"{data[0]} removed from the database.")
    else:
        print(f"{data[0]} not found in the database.")
    conn.close()





def find_barcodes_in_image(file_path, treeview):
    img = cv2.imread(file_path)
    barcodes = decode(img)
    if not barcodes:
        print("No barcodes found")
    else:
        for barcode in barcodes:
            # Decode the barcode data and add leading zeros if necessary to ensure 13 digits
            barcode_data = barcode.data.decode('utf-8').zfill(13)
            treeview.insert('', 'end', values=(barcode_data,))
            save_to_db(barcode_data)


def browse_file(treeview):
    filename = filedialog.askopenfilename()
    if filename:
        find_barcodes_in_image(filename, treeview)

def remove_item(treeview):
    selected_items = treeview.selection()
    if selected_items:
        selected_item = selected_items[0]
        data = treeview.item(selected_item)['values'][0]
        # Pad the data with leading zeros to make it 13 digits long
        data_padded = str(data).zfill(13)
        remove_from_db((data_padded,))
        treeview.delete(selected_item)
    else:
        print("No item selected")



def main():
    root = tk.Tk()
    treeview = ttk.Treeview(root, columns=("Barcode"), show='headings')
    treeview.heading("Barcode", text="Barcode")
    treeview.pack()

    for row in load_from_db():
        treeview.insert('', 'end', values=row)

    browse_button = ttk.Button(root, text="Browse", command=lambda: browse_file(treeview))
    browse_button.pack()

    remove_button = ttk.Button(root, text="Remove selected", command=lambda: remove_item(treeview))
    remove_button.pack()

    root.mainloop()

if __name__ == "__main__":
    main()
