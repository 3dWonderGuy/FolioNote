# Storage Model & Persistence

FolioNote uses a hybrid storage model: an open directory package bundle combining relational metadata with binary stroke streams.

---

## 📦 Package Bundle Structure (`.notebook`)

Each notebook is a self-contained directory on the file system:

```text
MyClassNotes.notebook/
├── structure.db                 # SQLite 3: Hierarchy, tags, titles, sorting
└── pages/
    ├── a1b2c3d4-0001.ink        # Compressed binary vector geometry
    ├── a1b2c3d4-0002.ink
    └── assets/
        └── img_991823.png       # Embedded canvas images and PDF cache
