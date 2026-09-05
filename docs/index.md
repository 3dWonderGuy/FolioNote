---
title: Home
---

# FolioNote

**FolioNote** is a cross-platform, infinite-canvas note-taking engine and document workspace built in C++20. Engineered from the ground up for low-latency active stylus input, fluid infinite navigation, and rich spatial organization. Perfect tool for anyone wanting to take digital notes of any kind.

---

## 💡 What is FolioNote?

Traditional note-taking tools force your thoughts into rigid constraints: fixed A4 page boundaries, sluggish web-based raster engines, proprietary cloud locks, or sluggish stylus tracking.

**FolioNote changes this paradigm:**

* **Boundless Spatial Freedom:** An infinite 2D canvas with seamless pan and zoom capabilities, free from artificial page margins or canvas borders.
* **Direct-to-Hardware Inking:** Built in native modern C++20 to bypass heavy web runtimes (Electron/WebTech), achieving native 120+ FPS canvas rendering and direct 480 Hz digitizer polling.
* **Organized Hierarchy:** Combines freeform spatial canvases with a structured document model—organizing your work into **Notebooks**, **Sections**, and **Pages**.
* **Open & Local-First:** Your data belongs entirely to you. FolioNote operates locally with lightweight file package bundles (`.notebook`) combining open SQLite metadata with compressed vector stroke streams.

---

## ✨ What Makes FolioNote Special

* ✍️ **Paper-Like Inking**  
  Writing feels natural and responsive. The pen engine captures your handwriting smoothly with zero noticeable lag, filtering out jitter so strokes always look clean.

* 📁 **Effortless Organization**  
  Keep projects structured without losing freeform flexibility. Group related ideas into clear **Notebooks**, **Sections**, and **Pages** that are easy to browse and rearrange.

* 🔍 **Notebook-Wide Search**  
  Find what you need instantly. Search across all your notebooks and pages at once to quickly pull up notes, headings, and topics.

* 📄 **Markdown & PDF Import / Export**  
  Import PDFs directly onto the canvas to annotate and mark them up, or export your work cleanly into PDF and standard Markdown to share with anyone.

* ♾️ **Truly Infinite Canvas**  
  Never run out of room mid-thought. Pan and zoom across massive canvases smoothly without stutter or frame drops.

* 🔒 **Private & Local-First**  
  Everything is stored right on your device. No required accounts, no forced subscriptions, and no cloud dependency.

---

## 🚀 Explore the Documentation

* 📐 **[Architecture Overview](architecture/overview.md)** — Core subsystems, runtime components, and data flow pipelines.
* 🔬 **[Mathematics & Foundations](math/overview.md)** — Stroke geometry, spline smoothing algorithms, and R-Tree spatial partitioning.
* 🛠️ **[Build & Setup Guide](dev/build.md)** — Step-by-step instructions for compiling on desktop systems and Android.
* 🤝 **[Contributing Guidelines](dev/contributing.md)** — Code conventions, branching workflows, and pull request guidelines.
