import tkinter as tk
from tkinter import ttk, messagebox
from bs4 import BeautifulSoup
import requests
import sqlite3

# Create a database or connect to one
conn = sqlite3.connect('links.db')
c = conn.cursor()

# Create table
c.execute("""CREATE TABLE IF NOT EXISTS links (
        source text,
        link text
    )""")

def scrape_links(source_url):
    try:
        response = requests.get(source_url)
        soup = BeautifulSoup(response.text, 'html.parser')
        return [a['href'] for a in soup.find_all('a', href=True)]
    except Exception as e:
        messagebox.showerror("Error", e)

def add_links_to_db(source, links):
    for link in links:
        c.execute("INSERT INTO links VALUES (:source, :link)", {'source': source, 'link': link})
        conn.commit()

def fetch_links():
    source = source_entry.get()
    links = scrape_links(source)
    if links:
        add_links_to_db(source, links)
        build_tree()
    else:
        messagebox.showinfo("Info", "No links found.")

def delete_links():
    selected_items = tree.selection()
    for selected_item in selected_items:
        c.execute("DELETE from links WHERE link=?", (tree.item(selected_item)["values"][1],))
        tree.delete(selected_item)
    conn.commit()

def build_tree():
    for i in tree.get_children():
        tree.delete(i)
    c.execute("SELECT * FROM links")
    records = c.fetchall()
    sources = {}
    for record in records:
        if record[0] not in sources:
            parent = tree.insert('', 'end', text=record[0])
            sources[record[0]] = parent
        tree.insert(sources[record[0]], 'end', text=record[1])

root = tk.Tk()
root.geometry("500x500")

source_entry = tk.Entry(root)
source_entry.pack()

add_button = tk.Button(root, text="Add", command=fetch_links)
add_button.pack()

remove_button = tk.Button(root, text="Remove", command=delete_links)
remove_button.pack()

tree = ttk.Treeview(root)
tree.pack(fill=tk.BOTH, expand=1)

root.mainloop()

# Close connection
conn.close()
