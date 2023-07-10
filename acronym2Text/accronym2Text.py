from tkinter import messagebox, Menu
import pystray
from PIL import Image
import os
import json
import threading
import tkinter as tk
from pynput import keyboard
from pynput.keyboard import Controller
import time
import sys
import pyperclip

class Acronym2Text:
    def __init__(self, abbreviations_file):
        self.abbreviations_file = abbreviations_file
        if os.path.exists(self.abbreviations_file):
            with open(self.abbreviations_file, 'r') as file:
                data = json.load(file)
                if "acronyms" in data:
                    self.abbreviations = {acronym["abbreviation"].lower(): acronym["explanation"] for acronym in data["acronyms"]}
                else:
                    self.abbreviations = {}
        else:
            self.abbreviations = {}
        self.buffer = ''
        self.keyboard_controller = Controller()
        self.last_key_press_time = time.time()
        self.last_shown_acronym = None
        self.is_autocomplete_active = True

    def add_abbreviation(self, abbreviation, expansion):
        self.abbreviations[abbreviation.lower()] = expansion
        self.save_abbreviations()

    def remove_abbreviation(self, abbreviation):
        del self.abbreviations[abbreviation.lower()]
        self.save_abbreviations()

    def save_abbreviations(self):
        acronyms = [{"abbreviation": abbr, "explanation": exp} for abbr, exp in self.abbreviations.items()]
        data = {"acronyms": acronyms}
        with open(self.abbreviations_file, 'w') as file:
            json.dump(data, file, indent=4)

    def on_press(self, key):
        if self.is_autocomplete_active:
            try:
                if time.time() - self.last_key_press_time > 1.0:
                    self.check_buffer(key.char)
                    self.buffer = ''
                temp_buffer = self.buffer + key.char
                expansion = self.abbreviations.get(temp_buffer.lower())
                if expansion:
                    self.buffer = ''
                    for _ in range(len(temp_buffer)):
                        self.keyboard_controller.press(keyboard.Key.backspace)
                        self.keyboard_controller.release(keyboard.Key.backspace)
                    self.keyboard_controller.type(expansion)
                else:
                    self.buffer += key.char
                self.last_key_press_time = time.time()
            except AttributeError:
                if key == keyboard.Key.space or key == keyboard.Key.enter:
                    self.check_buffer('')
                    self.buffer = ''
                elif key == keyboard.Key.backspace and len(self.buffer) > 0:
                    self.buffer = self.buffer[:-1]

    def check_buffer(self, current_key):
        self.buffer += current_key
        expansion = self.abbreviations.get(self.buffer.lower())
        if expansion:
            for _ in range(len(self.buffer)):
                self.keyboard_controller.press(keyboard.Key.backspace)
                self.keyboard_controller.release(keyboard.Key.backspace)
            self.keyboard_controller.type(expansion)

    def start(self):
        with keyboard.Listener(on_press=self.on_press) as listener:
            listener.join()

    def get_highlighted_text(self):
        return pyperclip.paste()

    def show_explanation(self, text):
        expansion = self.abbreviations.get(text.lower())
        if expansion and text.lower() != self.last_shown_acronym:
            messagebox.showinfo("Acronym Explanation", f"{text}: {expansion}")
            self.last_shown_acronym = text.lower()

    def add_to_acronyms(self, text):
        if text not in self.abbreviations.values():
            # Add the text to the acronym list
            self.add_abbreviation(text.lower(), '')

abbreviations_file = 'preferences.json'
acronym2text = Acronym2Text(abbreviations_file)

root = tk.Tk()
root.title("Acronym2Text")

abbreviation_label = tk.Label(root, text="Abbreviation:")
abbreviation_label.pack()
abbreviation_entry = tk.Entry(root)
abbreviation_entry.pack()

expansion_label = tk.Label(root, text="Expansion:")
expansion_label.pack()
expansion_entry = tk.Entry(root)
expansion_entry.pack()

add_button = tk.Button(root, text="Add Abbreviation", command=lambda: acronym2text.add_abbreviation(abbreviation_entry.get(), expansion_entry.get()))
add_button.pack()

remove_button = tk.Button(root, text="Remove Abbreviation", command=lambda: acronym2text.remove_abbreviation(abbreviation_entry.get()))
remove_button.pack()

keyboard_listener_thread = threading.Thread(target=acronym2text.start)
keyboard_listener_thread.start()

def check_highlighted_text():
    while True:
        highlighted_text = acronym2text.get_highlighted_text()
        if highlighted_text:
            acronym2text.show_explanation(highlighted_text)
        time.sleep(0.5)

highlighted_text_thread = threading.Thread(target=check_highlighted_text)
highlighted_text_thread.start()

# Create an empty icon
icon = Image.new('1', (64, 64), color=0)

def exit_action(icon, item):
    global stop_threads
    stop_threads = True  # Stop the keyboard listener thread
    root.quit()  # Stop the tkinter mainloop
    icon.stop()  # Stop the pystray icon
    try:
        sys.exit()  # Exit the program
    except SystemExit:
        pass

def hide_window():
    root.withdraw()

def show_window(icon, item):
    root.after(0, root.deiconify)

def add_to_acronyms_menu():
    highlighted_text = acronym2text.get_highlighted_text()
    if highlighted_text and highlighted_text.lower() not in acronym2text.abbreviations:
        acronym2text.add_to_acronyms(highlighted_text)

menu = (
    pystray.MenuItem('Show', show_window),
    pystray.MenuItem('Quit', exit_action),
    pystray.MenuItem('Add to Acronyms', add_to_acronyms_menu)
)
sys_tray_icon = pystray.Icon("name", icon, "Acronym2Text", menu)

root.protocol('WM_DELETE_WINDOW', hide_window)

def run_icon():
    sys_tray_icon.run()

icon_thread = threading.Thread(target=run_icon)
icon_thread.start()

def disable_autocomplete(event):
    acronym2text.is_autocomplete_active = False

def enable_autocomplete(event):
    acronym2text.is_autocomplete_active = True

abbreviation_entry.bind("<FocusIn>", disable_autocomplete)
abbreviation_entry.bind("<FocusOut>", enable_autocomplete)

root.mainloop()
