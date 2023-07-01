import sqlite3
import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import requests
from bs4 import BeautifulSoup
from PIL import Image
from io import BytesIO
import os


class Application(tk.Tk):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.title("Website Image Metadata Scraper")

        # Initialize database
        self.conn = sqlite3.connect(':memory:')
        self.c = self.conn.cursor()
        self.c.execute("CREATE TABLE images (id INTEGER PRIMARY KEY, url TEXT, width INT, height INT, notes TEXT)")
        self.conn.commit()

        # Define GUI components
        self.tree = ttk.Treeview(self)
        self.tree["columns"] = ("URL", "Width", "Height", "Notes")
        for col in self.tree["columns"]:
            self.tree.heading(col, text=col)
        self.tree.pack()

        tk.Label(self, text='URL:').pack()
        self.entry_url = tk.Entry(self)
        self.entry_url.pack()

        tk.Label(self, text='Notes:').pack()
        self.entry_notes = tk.Entry(self)
        self.entry_notes.pack()

        self.button_add = tk.Button(self, text="Add", command=self.add_image)
        self.button_add.pack()

        self.button_edit = tk.Button(self, text="Edit", command=self.edit_image)
        self.button_edit.pack()

        self.button_remove = tk.Button(self, text="Remove", command=self.remove_image)
        self.button_remove.pack()

    def scrape_website(self, url):
        response = requests.get(url)
        soup = BeautifulSoup(response.text, 'html.parser')
        img_tags = soup.find_all('img')

        for img in img_tags:
            img_url = os.path.join(url, img.get('src'))
            response = requests.get(img_url)
            img_obj = Image.open(BytesIO(response.content))

            self.c.execute("INSERT INTO images (url, width, height) VALUES (?, ?, ?)",
                           (img_url, img_obj.width, img_obj.height))
            self.conn.commit()

    def add_image(self):
        url = self.entry_url.get()
        notes = self.entry_notes.get()

        try:
            self.scrape_website(url)
            self.c.execute("UPDATE images SET notes=? WHERE url=?", (notes, url))
            self.conn.commit()
            self.update_treeview()
            messagebox.showinfo("Success", "Image added successfully.")
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def edit_image(self):
        selected = self.tree.selection()
        if selected:
            url = self.entry_url.get()
            notes = self.entry_notes.get()

            try:
                self.c.execute("UPDATE images SET notes=? WHERE url=?", (notes, url))
                self.conn.commit()
                self.update_treeview()
                messagebox.showinfo("Success", "Image edited successfully.")
            except Exception as e:
                messagebox.showerror("Error", str(e))
        else:
            messagebox.showwarning("No selection", "No image selected.")

    def remove_image(self):
        selected = self.tree.selection()
        if selected:
            url = self.tree.set(selected, "URL")
            try:
                self.c.execute("DELETE FROM images WHERE url=?", (url,))
                self.conn.commit()
                self.update_treeview()
                messagebox.showinfo("Success", "Image removed successfully.")
            except Exception as e:
                messagebox.showerror("Error", str(e))
        else:
            messagebox.showwarning("No selection", "No image selected.")

    def update_treeview(self):
        for i in self.tree.get_children():
            self.tree.delete(i)
        self.c.execute("SELECT * FROM images")
        for row in self.c.fetchall():
            self.tree.insert('', 'end', values=row[1:])


app = Application()
app.mainloop()
