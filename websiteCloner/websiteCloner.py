import os
import re
import webbrowser
import sqlite3
import tkinter as tk
from tkinter import messagebox
from bs4 import BeautifulSoup
from urllib.request import urlopen
from tkinter import ttk

DATABASE = 'clone_data.db'

# Timeout so a slow or unresponsive host cannot freeze the GUI indefinitely.
REQUEST_TIMEOUT = 15


class Application(tk.Tk):
    def __init__(self):
        super().__init__()

        self.title("Website Cloner")
        self.geometry("400x400")

        self.conn = sqlite3.connect(DATABASE)
        self.cursor = self.conn.cursor()

        self.cursor.execute(
            '''CREATE TABLE IF NOT EXISTS clones
            (url TEXT PRIMARY KEY, path TEXT)'''
        )

        url_label = tk.Label(self, text="Website URL:")
        url_label.pack()

        self.url_entry = tk.Entry(self, width=50)
        self.url_entry.pack()

        clone_button = tk.Button(self, text="Clone", command=self.clone)
        clone_button.pack()

        self.tree = ttk.Treeview(self)
        self.tree["columns"] = ("one", "two")
        self.tree.column("#0", width=270)
        self.tree.column("one", width=100)
        self.tree.column("two", width=100)
        self.tree.heading("#0", text="URL")
        self.tree.heading("one", text="File Path")

        self.tree.bind("<Double-1>", self.open_html)
        self.tree.pack()

        self.load_data()

    def clone(self):
        url = self.url_entry.get()
        if not url:
            messagebox.showerror("Error", "Please enter a URL")
            return

        try:
            html = urlopen(url, timeout=REQUEST_TIMEOUT)
            soup = BeautifulSoup(html, 'html.parser')

            if not os.path.isdir('WebsiteClones'):
                os.makedirs('WebsiteClones')

            # Replace every character that is unsafe in a filename (not just
            # "/"), so URLs containing "?", ":", "&", etc. do not produce
            # paths that are illegal on Windows. Also cap the length.
            stripped = re.sub(r'^https?://', '', url)
            safe = re.sub(r'[^A-Za-z0-9._-]', '_', stripped)[:150] or 'clone'
            filename = safe + '.html'
            path = os.path.join('WebsiteClones', filename)

            # Explicit UTF-8 so non-ASCII pages do not fail on platforms whose
            # default encoding is not UTF-8 (e.g. Windows).
            with open(path, 'w', encoding='utf-8') as f:
                f.write(str(soup.prettify()))

            self.cursor.execute(
                'INSERT OR REPLACE INTO clones VALUES (?, ?)',
                (url, path)
            )
            self.conn.commit()

            self.load_data()
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def load_data(self):
        self.tree.delete(*self.tree.get_children())
        self.cursor.execute('SELECT * FROM clones')
        for row in self.cursor.fetchall():
            self.tree.insert("", "end", text=row[0], values=(row[1],))

    def open_html(self, event):
        selection = self.tree.selection()
        if not selection:
            # Double-clicking empty space yields no selection; the old code
            # indexed [0] unconditionally and raised IndexError.
            return
        values = self.tree.item(selection[0], "values")
        if not values:
            return
        path = values[0]
        webbrowser.open('file://' + os.path.realpath(path))


if __name__ == "__main__":
    app = Application()
    app.mainloop()
