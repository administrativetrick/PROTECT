import { invoke } from "@tauri-apps/api/core";
import { open as openDialog } from "@tauri-apps/plugin-dialog";
import "./style.css";

// ---------- tiny DOM helpers ----------
type Props = { [k: string]: any };
function el<K extends keyof HTMLElementTagNameMap>(
  tag: K, props: Props = {}, children: (Node | string)[] = []
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === "class") node.className = v;
    else if (k === "html") node.innerHTML = v;
    else if (k.startsWith("on") && typeof v === "function")
      node.addEventListener(k.slice(2), v);
    else if (v !== undefined && v !== null) node.setAttribute(k, String(v));
  }
  for (const c of children) node.append(c instanceof Node ? c : document.createTextNode(c));
  return node;
}
function clear(n: HTMLElement) { while (n.firstChild) n.removeChild(n.firstChild); }

function toast(msg: string, err = false) {
  const box = document.getElementById("toast")!;
  const t = el("div", { class: "toast" + (err ? " err" : "") }, [msg]);
  box.append(t);
  setTimeout(() => t.remove(), err ? 6000 : 3500);
}
async function call<T>(cmd: string, args: Props = {}): Promise<T | undefined> {
  try { return await invoke<T>(cmd, args); }
  catch (e) { toast(String(e), true); return undefined; }
}

function pageHead(title: string, desc: string): HTMLElement {
  return el("div", { class: "page-head" }, [el("h1", {}, [title]), el("p", {}, [desc])]);
}
function emptyRow(text: string) { return el("div", { class: "empty" }, [text]); }

// ---------- Tool: ChatLogsParser ----------
async function viewChatlog(root: HTMLElement) {
  clear(root);
  root.append(pageHead("Chat Logs Parser",
    "Scan chat logs for a keyword list. Matches are whole-word and case-insensitive; keywords are treated literally, so punctuation never breaks the scan."));

  const ta = el("textarea", { placeholder: "Paste chat logs here, one message per line..." });
  const summary = el("pre", { class: "mono" });
  const resultsCard = el("div", { class: "card" });

  const scanBtn = el("button", { class: "btn", onclick: async () => {
    const out = await call<{ summary: string; saved_path: string; matched: boolean }>(
      "chatlog_scan", { text: (ta as HTMLTextAreaElement).value });
    if (out) { summary.textContent = out.summary + "\nsaved: " + out.saved_path; refresh(); }
  }}, ["Scan"]);

  const kwBtn = el("button", { class: "btn ghost", onclick: async () => {
    const current = (await call<string[]>("chatlog_get_keywords")) || [];
    const edit = el("input", { type: "text", value: current.join(", ") });
    const dlg = el("div", { class: "card" }, [
      el("label", { class: "field" }, [el("span", {}, ["Keywords (comma separated)"]), edit]),
      el("button", { class: "btn sm", onclick: async () => {
        const kws = (edit as HTMLInputElement).value.split(",").map(s => s.trim()).filter(Boolean);
        await call("chatlog_set_keywords", { keywords: kws });
        toast("Keywords saved"); kwWrap.replaceChildren();
      }}, ["Save"]),
    ]);
    kwWrap.replaceChildren(dlg);
  }}, ["Edit Keywords"]);

  const kwWrap = el("div", {});

  root.append(
    el("div", { class: "card" }, [
      el("label", { class: "field" }, [el("span", {}, ["Chat log"]), ta]),
      el("div", { class: "row" }, [scanBtn, kwBtn]),
      kwWrap,
      summary,
    ]),
    resultsCard,
  );

  async function refresh() {
    const rows = (await call<{ chat_log: string; result: string }[]>("chatlog_list")) || [];
    clear(resultsCard);
    resultsCard.append(el("h3", {}, ["Saved results"]));
    if (!rows.length) { resultsCard.append(emptyRow("No saved scans yet.")); return; }
    const tbl = el("table", {}, [el("thead", {}, [el("tr", {}, [
      el("th", {}, ["Chat Log"]), el("th", {}, ["Result"]), el("th", {}, [""]),
    ])])]);
    const tb = el("tbody");
    for (const r of rows) {
      tb.append(el("tr", {}, [
        el("td", { class: "wrap" }, [r.chat_log]),
        el("td", { class: "mono" }, [r.result]),
        el("td", {}, [el("button", { class: "btn danger", onclick: async () => {
          await call("chatlog_delete", { chatLog: r.chat_log, result: r.result }); refresh();
        }}, ["Delete"])]),
      ]));
    }
    tbl.append(tb); resultsCard.append(tbl);
  }
  refresh();
}

// ---------- Tool: acronym2Text ----------
async function viewAcronym(root: HTMLElement) {
  clear(root);
  root.append(pageHead("Acronym2Text",
    "Manage the acronym dictionary, expand acronyms in a block of text, and optionally run a global keyboard hook that expands abbreviations as you type (needs an X11 desktop session)."));

  const listCard = el("div", { class: "card" });
  const abbr = el("input", { type: "text", placeholder: "ASL" });
  const exp = el("input", { type: "text", placeholder: "Age/Sex/Location" });

  const addRow = el("div", { class: "card" }, [
    el("div", { class: "row" }, [
      el("label", { class: "field", style: "flex:1" }, [el("span", {}, ["Abbreviation"]), abbr]),
      el("label", { class: "field", style: "flex:2" }, [el("span", {}, ["Explanation"]), exp]),
    ]),
    el("div", { class: "row" }, [
      el("button", { class: "btn", onclick: async () => {
        await call("acronym_add", { abbreviation: (abbr as HTMLInputElement).value, explanation: (exp as HTMLInputElement).value });
        (abbr as HTMLInputElement).value = ""; (exp as HTMLInputElement).value = ""; refresh();
      }}, ["Add / Update"]),
      el("button", { class: "btn ghost", onclick: async () => {
        await call("acronym_remove", { abbreviation: (abbr as HTMLInputElement).value }); refresh();
      }}, ["Remove"]),
    ]),
  ]);

  const dot = el("span", { class: "statusdot" });
  const hookLabel = el("span", {}, ["checking..."]);
  const hookBtn = el("button", { class: "btn ghost", onclick: async () => {
    const on = await call<boolean>("acronym_hook_status");
    if (on) { await call("acronym_hook_stop"); } else { await call("acronym_hook_start"); }
    updateHook();
  }}, ["Toggle"]);
  async function updateHook() {
    const on = (await call<boolean>("acronym_hook_status")) || false;
    dot.className = "statusdot" + (on ? " on" : "");
    hookLabel.textContent = on ? "Keyboard hook: running" : "Keyboard hook: stopped";
  }
  const hookCard = el("div", { class: "card" }, [
    el("div", { class: "row" }, [dot, hookLabel, hookBtn]),
    el("p", { class: "empty" }, ["The global hook captures typing system-wide and injects the expansion. It requires an X11 session (RECORD/XTEST); it is a no-op where unavailable."]),
  ]);

  const expandTa = el("textarea", { placeholder: "Type text using acronyms; Expand replaces them with their full forms." });
  const expandOut = el("pre", { class: "mono" });
  const expandCard = el("div", { class: "card" }, [
    el("label", { class: "field" }, [el("span", {}, ["Expand text"]), expandTa]),
    el("button", { class: "btn", onclick: async () => {
      const out = await call<string>("acronym_expand_text", { text: (expandTa as HTMLTextAreaElement).value });
      if (out !== undefined) expandOut.textContent = out;
    }}, ["Expand"]),
    expandOut,
  ]);

  root.append(addRow, listCard, expandCard, hookCard);

  async function refresh() {
    const rows = (await call<{ abbreviation: string; explanation: string }[]>("acronym_list")) || [];
    clear(listCard);
    listCard.append(el("h3", {}, ["Dictionary"]));
    if (!rows.length) { listCard.append(emptyRow("No acronyms yet.")); return; }
    const tbl = el("table", {}, [el("thead", {}, [el("tr", {}, [
      el("th", {}, ["Abbreviation"]), el("th", {}, ["Explanation"]), el("th", {}, [""]),
    ])])]);
    const tb = el("tbody");
    for (const r of rows) {
      tb.append(el("tr", {}, [
        el("td", {}, [r.abbreviation.toUpperCase()]),
        el("td", {}, [r.explanation]),
        el("td", {}, [el("button", { class: "btn danger", onclick: async () => {
          await call("acronym_remove", { abbreviation: r.abbreviation }); refresh();
        }}, ["Delete"])]),
      ]));
    }
    tbl.append(tb); listCard.append(tbl);
  }
  refresh();
  updateHook();
}

// ---------- Tool: linkScraper ----------
async function viewLinks(root: HTMLElement) {
  clear(root);
  root.append(pageHead("Link Scraper",
    "Fetch a page and store every link, resolved to an absolute URL, grouped by its source page."));
  const src = el("input", { type: "text", placeholder: "https://example.com" });
  const listCard = el("div", { class: "card" });
  root.append(el("div", { class: "card" }, [
    el("label", { class: "field" }, [el("span", {}, ["Source URL"]), src]),
    el("button", { class: "btn", onclick: async () => {
      const n = await call<number>("links_scrape", { source: (src as HTMLInputElement).value });
      if (n !== undefined) { toast(`${n} link(s) stored`); refresh(); }
    }}, ["Scrape"]),
  ]), listCard);

  async function refresh() {
    const rows = (await call<{ source: string; link: string }[]>("links_list")) || [];
    clear(listCard);
    listCard.append(el("h3", {}, ["Stored links"]));
    if (!rows.length) { listCard.append(emptyRow("Nothing scraped yet.")); return; }
    const groups = new Map<string, string[]>();
    for (const r of rows) { if (!groups.has(r.source)) groups.set(r.source, []); groups.get(r.source)!.push(r.link); }
    for (const [source, links] of groups) {
      const head = el("div", { class: "group-head" }, []);
      head.append(el("span", {}, [source]), document.createTextNode(" "),
        el("button", { class: "btn danger sm", onclick: async () => {
          await call("links_delete_source", { source }); refresh();
        }}, ["Delete source"]));
      const ul = el("ul", { class: "linklist" });
      for (const link of links) {
        ul.append(el("li", {}, [
          el("a", { href: link, target: "_blank" }, [link]),
          el("button", { class: "btn danger sm", onclick: async () => {
            await call("links_delete_link", { source, link }); refresh();
          }}, ["x"]),
        ]));
      }
      listCard.append(head, ul);
    }
  }
  refresh();
}

// ---------- Tool: websiteCloner ----------
async function viewClone(root: HTMLElement) {
  clear(root);
  root.append(pageHead("Website Cloner",
    "Save a page's HTML for later review. Images are intentionally not downloaded."));
  const url = el("input", { type: "text", placeholder: "https://example.com" });
  const listCard = el("div", { class: "card" });
  root.append(el("div", { class: "card" }, [
    el("label", { class: "field" }, [el("span", {}, ["Website URL"]), url]),
    el("button", { class: "btn", onclick: async () => {
      const r = await call<{ url: string; path: string }>("clone_site", { url: (url as HTMLInputElement).value });
      if (r) { toast("Saved " + r.path); refresh(); }
    }}, ["Clone"]),
  ]), listCard);

  async function refresh() {
    const rows = (await call<{ url: string; path: string }[]>("clone_list")) || [];
    clear(listCard);
    listCard.append(el("h3", {}, ["Clones"]));
    if (!rows.length) { listCard.append(emptyRow("No clones yet.")); return; }
    const tbl = el("table", {}, [el("thead", {}, [el("tr", {}, [
      el("th", {}, ["URL"]), el("th", {}, ["Path"]), el("th", {}, [""]),
    ])])]);
    const tb = el("tbody");
    for (const r of rows) {
      tb.append(el("tr", {}, [
        el("td", { class: "wrap" }, [r.url]),
        el("td", { class: "wrap" }, [r.path]),
        el("td", {}, [el("button", { class: "btn sm ghost", onclick: async () => {
          await call("clone_open", { path: r.path });
        }}, ["Open"])]),
      ]));
    }
    tbl.append(tb); listCard.append(tbl);
  }
  refresh();
}

// ---------- Tool: ImageMetadataScraper ----------
async function viewImgMeta(root: HTMLElement) {
  clear(root);
  root.append(pageHead("Image Metadata Scraper",
    "Scrape a page for images and record each image's format, dimensions, and any EXIF metadata. The images themselves are not stored."));
  const url = el("input", { type: "text", placeholder: "https://example.com" });
  const notes = el("input", { type: "text", placeholder: "case notes (optional)" });
  const filter = el("input", { type: "text", placeholder: "filter by URL or key" });
  const listCard = el("div", { class: "card" });
  root.append(el("div", { class: "card" }, [
    el("label", { class: "field" }, [el("span", {}, ["Page URL"]), url]),
    el("label", { class: "field" }, [el("span", {}, ["Notes"]), notes]),
    el("button", { class: "btn", onclick: async () => {
      const n = await call<number>("imgmeta_scrape", { url: (url as HTMLInputElement).value, notes: (notes as HTMLInputElement).value });
      if (n !== undefined) { toast(`${n} image(s) recorded`); refresh(); }
    }}, ["Scrape"]),
  ]),
  el("div", { class: "card" }, [
    el("div", { class: "row" }, [
      el("label", { class: "field", style: "flex:1;margin:0" }, [el("span", {}, ["Filter"]), filter]),
      el("button", { class: "btn ghost", onclick: () => refresh() }, ["Apply"]),
    ]),
  ]),
  listCard);

  async function refresh() {
    const f = (filter as HTMLInputElement).value;
    const rows = (await call<{ url: string; key: string; value: string; notes: string }[]>("imgmeta_list", { filter: f || null })) || [];
    clear(listCard);
    listCard.append(el("h3", {}, ["Image metadata"]));
    if (!rows.length) { listCard.append(emptyRow("Nothing recorded yet.")); return; }
    const tbl = el("table", {}, [el("thead", {}, [el("tr", {}, [
      el("th", {}, ["Image URL"]), el("th", {}, ["Key"]), el("th", {}, ["Value"]), el("th", {}, ["Notes"]), el("th", {}, [""]),
    ])])]);
    const tb = el("tbody");
    for (const r of rows) {
      tb.append(el("tr", {}, [
        el("td", { class: "wrap" }, [r.url]),
        el("td", {}, [r.key]),
        el("td", { class: "wrap" }, [r.value]),
        el("td", {}, [r.notes]),
        el("td", {}, [
          el("button", { class: "btn sm ghost", onclick: async () => {
            const nn = prompt("New notes:", r.notes);
            if (nn !== null) { await call("imgmeta_edit_notes", { url: r.url, notes: nn }); refresh(); }
          }}, ["Edit"]),
          el("button", { class: "btn danger sm", onclick: async () => {
            await call("imgmeta_delete", { url: r.url }); refresh();
          }}, ["Delete"]),
        ]),
      ]));
    }
    tbl.append(tb); listCard.append(tbl);
  }
  refresh();
}

// ---------- Tool: imageBarcodeSearch ----------
async function viewBarcode(root: HTMLElement) {
  clear(root);
  root.append(pageHead("Image Barcode Search",
    "Decode barcodes and QR codes from an image file. Numeric EAN/UPC codes are padded to 13 digits; other payloads are kept verbatim."));
  const listCard = el("div", { class: "card" });
  root.append(el("div", { class: "card" }, [
    el("button", { class: "btn", onclick: async () => {
      const file = await openDialog({ multiple: false, filters: [{ name: "Images", extensions: ["png", "jpg", "jpeg"] }] });
      if (typeof file === "string") {
        const res = await call<{ kind: string; data: string }[]>("barcode_scan", { path: file });
        if (res) {
          if (!res.length) toast("No barcodes found");
          else toast(`${res.length} code(s) decoded`);
          refresh();
        }
      }
    }}, ["Browse image"]),
  ]), listCard);

  async function refresh() {
    const rows = (await call<string[]>("barcode_list")) || [];
    clear(listCard);
    listCard.append(el("h3", {}, ["Decoded barcodes"]));
    if (!rows.length) { listCard.append(emptyRow("No barcodes yet.")); return; }
    const tbl = el("table", {}, [el("thead", {}, [el("tr", {}, [el("th", {}, ["Barcode"]), el("th", {}, [""])])])]);
    const tb = el("tbody");
    for (const d of rows) {
      tb.append(el("tr", {}, [
        el("td", { class: "mono" }, [d]),
        el("td", {}, [el("button", { class: "btn danger sm", onclick: async () => {
          await call("barcode_delete", { data: d }); refresh();
        }}, ["Delete"])]),
      ]));
    }
    tbl.append(tb); listCard.append(tbl);
  }
  refresh();
}

// ---------- shell / router ----------
type Tool = { id: string; label: string; icon: string; view: (r: HTMLElement) => void };
const TOOLS: Tool[] = [
  { id: "chatlog", label: "Chat Logs", icon: "💬", view: viewChatlog },
  { id: "acronym", label: "Acronyms", icon: "🔤", view: viewAcronym },
  { id: "links", label: "Link Scraper", icon: "🔗", view: viewLinks },
  { id: "clone", label: "Website Cloner", icon: "📄", view: viewClone },
  { id: "imgmeta", label: "Image Metadata", icon: "🖼️", view: viewImgMeta },
  { id: "barcode", label: "Barcode Search", icon: "🏷️", view: viewBarcode },
];

function buildSidebar(onSelect: (t: Tool) => void): HTMLElement[] {
  const brand = el("div", { class: "brand" }, [
    el("div", { class: "logo" }, ["P"]),
    el("div", {}, [el("div", { class: "title" }, ["PROTECT"]), el("div", { class: "sub" }, ["Investigation suite"])]),
  ]);
  const btns = TOOLS.map(t =>
    el("button", { class: "navbtn", "data-id": t.id, onclick: () => onSelect(t) }, [
      el("span", { class: "ico" }, [t.icon]), el("span", { class: "lbl" }, [t.label]),
    ]));
  const foot = el("div", { class: "sidebar-foot" }, ["Local-only. Data stored in your app data folder."]);
  return [brand, ...btns, foot];
}

function main() {
  const sidebar = document.getElementById("sidebar")!;
  const mainEl = document.getElementById("main")!;
  const select = (t: Tool) => {
    document.querySelectorAll(".navbtn").forEach(b =>
      b.classList.toggle("active", b.getAttribute("data-id") === t.id));
    t.view(mainEl);
  };
  sidebar.append(...buildSidebar(select));
  select(TOOLS[0]);
}
main();
