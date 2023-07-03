import os
import webbrowser
import sqlite3
import tkinter as tk
from tkinter import messagebox
from bs4 import BeautifulSoup
from urllib.request import urlopen
from tkinter import ttk

DATABASE = 'clone_data.db'


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
            html = urlopen(url)
            soup = BeautifulSoup(html, 'html.parser')

            if not os.path.isdir('WebsiteClones'):
                os.makedirs('WebsiteClones')

            filename = url.replace("https://", "").replace("http://", "").replace("/", "_") + '.html'
            path = os.path.join('WebsiteClones', filename)

            with open(path, 'w') as f:
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
        item = self.tree.selection()[0]
        path = self.tree.item(item, "values")[0]
        webbrowser.open('file://' + os.path.realpath(path))


if __name__ == "__main__":
    app = Application()
    app.mainloop()
