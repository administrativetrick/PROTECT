import os
import json
import sqlite3
import tkinter as tk
from tkinter import simpledialog, messagebox
from tkinter import scrolledtext
from tkinter import ttk
import re

class KeywordSearchApp:
    def __init__(self, root):
        self.root = root
        root.grid_rowconfigure(0, weight=1)
        root.grid_columnconfigure(0, weight=1)

        # Load preferences
        try:
            with open('preferences.json', 'r') as f:
                self.keywords = json.load(f)
        except FileNotFoundError:
            self.keywords = ['keyword1', 'keyword2']

        # Database setup
        self.conn = sqlite3.connect('keyword_results.db')
        self.cursor = self.conn.cursor()
        self.cursor.execute('''
            CREATE TABLE IF NOT EXISTS results (
                id INTEGER PRIMARY KEY,
                chat_log TEXT,
            result TEXT
            )
        ''')

        # Create directory for storing chats if it doesn't exist
        if not os.path.exists('chats'):
            os.makedirs('chats')

        # Text Field
        self.text_field = scrolledtext.ScrolledText(root)
        self.text_field.grid(row=0, column=0, sticky='nsew')

        # Submit Button
        self.submit_button = tk.Button(root, text="Submit", command=self.parse_logs)
        self.submit_button.grid(row=1, column=0)

        # Preferences Button
        self.pref_button = tk.Button(root, text="Preferences", command=self.update_keywords)
        self.pref_button.grid(row=2, column=0)

        # Treeview for database results
        self.treeview = ttk.Treeview(root, columns=("chat_log", "result"), show="headings")
        self.treeview.column("chat_log", width=300)
        self.treeview.column("result", width=500)
        self.treeview.heading("chat_log", text="Chat Log")
        self.treeview.heading("result", text="Result")
        self.treeview.grid(row=3, column=0, sticky='nsew')

        self.refresh_treeview()

        # Add Button
        self.add_button = tk.Button(root, text="Add item", command=self.add_item)
        self.add_button.grid(row=4, column=0)

        # Remove Button
        self.remove_button = tk.Button(root, text="Remove item", command=self.remove_item)
        self.remove_button.grid(row=5, column=0)

    def load_preferences_into_field(self):
        preferences_str = ', '.join(self.keywords)
        self.text_field.insert(tk.END, preferences_str)

    def save_preferences(self):
        with open('preferences.json', 'w') as f:
            json.dump(self.keywords, f)

    def update_keywords(self):
        current_keywords = ', '.join(self.keywords)
        new_keywords = simpledialog.askstring("Preferences", "Enter new keywords separated by commas",
                                              initialvalue=current_keywords)
        if new_keywords is None:
            # User cancelled the dialog, do nothing
            return
        elif not new_keywords.strip():
            # User left the input field empty, show error
            messagebox.showerror("Error", "Input field cannot be empty")
        else:
            # Update the keywords
            self.keywords = [keyword.strip() for keyword in new_keywords.split(',')]
            self.save_preferences()

    def parse_logs(self):
        chat_logs = self.text_field.get("1.0", tk.END).strip()
        if not chat_logs:
            messagebox.showerror("Error", "Chat log field cannot be empty")
            return

        lines = chat_logs.split('\n')
        found_keywords = set()
        result_text = ""

        for idx, line in enumerate(lines, start=1):
            for keyword in self.keywords:
                if re.search(r'\b' + keyword + r'\b', line, re.IGNORECASE):
                    found_keywords.add(keyword)
                    result_text += f"Keyword '{keyword}' found at line {idx}\n"

        if not found_keywords:
            result_text = "Clean\n"

        # Save chat to file
        chat_file = os.path.join('chats', f'chat_{len(os.listdir("chats")) + 1}.txt')
        with open(chat_file, 'w') as file:
            file.write(chat_logs)

        # Save result to database
        self.cursor.execute('''
            INSERT INTO results (chat_log, result)
            VALUES (?, ?)
        ''', (chat_file, result_text))
        self.conn.commit()

        self.refresh_treeview()

    def refresh_treeview(self):
        for row in self.treeview.get_children():
            self.treeview.delete(row)

        self.cursor.execute('SELECT * FROM results')
        for row in self.cursor.fetchall():
            self.treeview.insert('', 'end', values=row[1:])

    def add_item(self):
        chat_log = simpledialog.askstring("Add item", "Enter chat log")
        result = simpledialog.askstring("Add item", "Enter result")
        if not chat_log or not result:
            messagebox.showerror("Error", "Input fields cannot be empty")
            return

        if not os.path.exists(chat_log):
            messagebox.showerror("Error", "Chat log file does not exist")
            return

        self.cursor.execute('''
            INSERT INTO results (chat_log, result)
            VALUES (?, ?)
        ''', (chat_log, result))
        self.conn.commit()
        self.refresh_treeview()

    def remove_item(self):
        if self.treeview.selection():
            selected_item = self.treeview.selection()[0]
            item_values = self.treeview.item(selected_item, 'values')
            self.cursor.execute('''
                DELETE FROM results
                WHERE chat_log = ? AND result = ?
            ''', item_values)
            self.conn.commit()
            self.refresh_treeview()
        else:
            messagebox.showerror("Error", "No item selected")

root = tk.Tk()
app = KeywordSearchApp(root)
root.mainloop()
