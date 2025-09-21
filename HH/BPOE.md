# BPOE.md — Hitchhiker Plan Session Continuity & Best Practices

This file accumulates best practices, recovery instructions, and session continuity notes
for the creation of the **Hitchhiker Plan** (HH) inside the Godspawn repo.

---

## 🔄 Session Recovery (if ChatGPT session crashes or bloats)

- Resume by reviewing the last committed chunks in `HH/_stitch/`
- Use `HH/TOC.md` to reconstruct scroll flow
- Check `megascroll.md` as auto-stitched product
- THIS FILE (`BPOE.md`) should include any **project-specific protocols**
  or instructions for continuing the HH session or handing it off

---

## 🧠 Notes to Self (and Successor AI Agents / Collaborators)

- All HH stitch chunks are tracked in `_stitch/`, named `0001_...`, `0002_...` etc
- `megascroll.md` is autostitched from `_stitch/*` and updated per commit
- Use PS7 watcher or manual `Join-Path` logic to append/check structure
- Update `TOC.md` manually or with auto-insert logic if possible
- Any *BPOE upgrades or refinements* to this process belong in **this file**

---

