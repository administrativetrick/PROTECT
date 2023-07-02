import os
import requests
from bs4 import BeautifulSoup
from PIL import Image, ExifTags
import tkinter as tk
from tkinter import messagebox
from tkinter import ttk
import sqlite3
import pickle


class Application(tk.Tk):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.title("Website Image Metadata Scraper")

        # Initialize database
        self.conn = sqlite3.connect('image_metadata.db')
        self.c = self.conn.cursor()
        self.c.execute(
            "CREATE TABLE IF NOT EXISTS images (id INTEGER PRIMARY KEY, url TEXT, metadata_key TEXT, metadata_value BLOB, notes TEXT)")
        self.conn.commit()

        # Define GUI components
        self.tree = ttk.Treeview(self)
        self.tree["columns"] = ("URL", "Metadata Key", "Metadata Value", "Notes")
        for col in self.tree["columns"]:
            self.tree.heading(col, text=col)
        self.tree.pack()

        tk.Label(self, text='URL:').pack()
        self.entry_url = tk.Entry(self)
        self.entry_url.pack()

        tk.Label(self, text='Notes:').pack()
        self.entry_notes = tk.Entry(self)
        self.entry_notes.pack()

        tk.Label(self, text='Filter:').pack()
        self.entry_filter = tk.Entry(self)
        self.entry_filter.pack()

        self.button_add = tk.Button(self, text="Add", command=self.add_image)
        self.button_add.pack()

        self.button_edit = tk.Button(self, text="Edit", command=self.edit_image)
        self.button_edit.pack()

        self.button_remove = tk.Button(self, text="Remove", command=self.remove_image)
        self.button_remove.pack()

        self.button_filter = tk.Button(self, text="Filter", command=self.filter_treeview)
        self.button_filter.pack()

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

                    # Extract EXIF data
                    exif_data = img_obj._getexif()
                    if exif_data is not None:
                        for tag, value in exif_data.items():
                            if tag in ExifTags.TAGS:
                                tag_name = ExifTags.TAGS[tag]
                            elif tag in ExifTags.GPSTAGS:
                                tag_name = ExifTags.GPSTAGS[tag]
                            else:
                                tag_name = tag

                            pickled_value = pickle.dumps(value)
                            image_data.append((img_url, tag_name, pickled_value))
                    else:
                        image_data.append(
                            (img_url, 'Dimensions', pickle.dumps({"Width": img_obj.width, "Height": img_obj.height})))

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

            for img_url, metadata_key, metadata_value in image_data:
                self.c.execute("INSERT INTO images (url, metadata_key, metadata_value, notes) VALUES (?, ?, ?, ?)",
                               (img_url, metadata_key, metadata_value, notes))

            self.conn.commit()
            self.update_treeview()
            messagebox.showinfo("Success", "Images added successfully.")
        except Exception as e:
            messagebox.showerror("Error", str(e))

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

    def update_treeview(self, filter_text=None):
        for row in self.tree.get_children():
            self.tree.delete(row)

        if filter_text:
            self.c.execute(
                "SELECT url, metadata_key, metadata_value, notes FROM images WHERE url LIKE ? OR metadata_key LIKE ?",
                ('%' + filter_text + '%', '%' + filter_text + '%'))
        else:
            self.c.execute("SELECT url, metadata_key, metadata_value, notes FROM images")

        rows = self.c.fetchall()

        for row in rows:
            url, metadata_key, metadata_value_blob, notes = row
            metadata_value = pickle.loads(metadata_value_blob)
            self.tree.insert('', 'end', values=(url, metadata_key, metadata_value, notes))

    def edit_image(self):
        selected = self.tree.selection()
        if selected:
            url = self.tree.set(selected[0], "URL")
            notes = simpledialog.askstring("Input", "Enter new notes:", parent=self)
            if notes is not None:
                try:
                    self.c.execute("UPDATE images SET notes=? WHERE url=?", (notes, url))
                    self.conn.commit()
                    self.update_treeview()
                    messagebox.showinfo("Success", "Image notes updated successfully.")
                except Exception as e:
                    messagebox.showerror("Error", str(e))
        else:
            messagebox.showwarning("No selection", "No image selected.")

    def filter_treeview(self):
        filter_text = self.entry_filter.get()
        self.update_treeview(filter_text=filter_text)


if __name__ == "__main__":
    app = Application()
    app.mainloop()
