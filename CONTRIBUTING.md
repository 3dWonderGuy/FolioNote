# Contributing Guidelines

Thank you for your interest in contributing! Whether you are fixing bugs, optimizing canvas math, or improving cross-platform stability, all contributions are welcome.

---

## 1. Branching Strategy

To keep official releases stable, this repository uses a staged branching model:

- **`main`**: Production and official release builds only. Do not open PRs directly against `main`.
- **`develop`**: Active integration branch where new features, math experiments, and fixes are merged. Pre-release test builds (APKs and binaries) are automatically generated from here.
- **`feature/*` or `fix/*`**: Isolated working branches for specific tasks.

---

## 2. Getting Started

1. **Fork the repository** and clone your fork locally:
   ```bash
   git clone [git@github.com:3dWonderGuy/FolioNote.git)
   cd YOUR_REPO
   ```
2. **Branch off `develop`**:
   ```bash
   git checkout develop
   git pull origin develop
   git checkout -b feature/your-feature-name
   # or
   git checkout -b fix/issue-description
   ```

---

## 3. Development Guidelines & Philosophy

- **Readability Over Hyper-Optimization:** Clear, understandable, and maintainable code is prioritized over clever micro-optimizations. Avoid deeply nested logic or overly obscure pointer tricks unless profiling proves a critical hot path bottleneck.
- **Consistent Common Formatting:** Keep formatting clean and match the style of the surrounding codebase (standard 4-space indentation, clear variable names, and logical function decomposition).
- **Cross-Platform Integrity:** Verify that changes do not break desktop builds or Android NDK targets.
- **Memory Safety:** Favor modern C++ standards, RAII, and standard containers over unmanaged manual allocations where feasible.

---

## 4. Submitting a Pull Request (PR)

1. Ensure your branch builds cleanly without compiler errors or warnings on your development platform.
2. Push your branch to your fork on GitHub.
3. Open a Pull Request **targeting the `develop` branch**.
4. Fill in the provided **Pull Request Template** completely with the context of your changes and platforms tested.

---

## 5. Reporting Bugs & Requesting Features

- Use the dedicated **Bug Report** and **Feature Request** issue forms available in the repository's Issues tab.
- For architectural questions, math discussions, or general inquiries, please use **GitHub Discussions** instead of opening an issue.
