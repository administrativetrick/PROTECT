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
        self.conn = sqlite3.connect('image_metadata.db')
        self.c = self.conn.cursor()
        self.c.execute(
            "CREATE TABLE IF NOT EXISTS images (id INTEGER PRIMARY KEY, url TEXT, width INT, height INT, notes TEXT)")
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

        # Update the treeview with any existing data from the database
        self.update_treeview()

    def scrape_website(self, url):
        try:
            response = requests.get(url)
            soup = BeautifulSoup(response.text, 'html.parser')
            img_tags = soup.find_all('img')

            image_data = []

            for img in img_tags:
                img_src = img.get('src')
                if not img_src:
                    continue

                img_url = img_src if img_src.startswith('http') else os.path.join(url, img_src)
                response = requests.get(img_url, stream=True)
                response.raw.decode_content = True

                try:
                    img_obj = Image.open(response.raw)
                    image_data.append((img_url, img_obj.width, img_obj.height))
                except Exception as e:
                    print(f"Couldn't open {img_url}: {e}")

            return image_data
        except Exception as e:
            print(f"Error while scraping {url}: {e}")
            return []

    def add_image(self):
        url = self.entry_url.get()
        notes = self.entry_notes.get()

        try:
            image_data = self.scrape_website(url)

            for img_url, width, height in image_data:
                self.c.execute("INSERT INTO images (url, width, height, notes) VALUES (?, ?, ?, ?)",
                               (img_url, width, height, notes))

            self.conn.commit()
            self.update_treeview()
            messagebox.showinfo("Success", "Images added successfully.")
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
            urls = [self.tree.set(item, "URL") for item in selected]
            try:
                for url in urls:
                    self.c.execute("DELETE FROM images WHERE url=?", (url,))
                self.conn.commit()
                self.update_treeview()
                messagebox.showinfo("Success", "Image(s) removed successfully.")
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
