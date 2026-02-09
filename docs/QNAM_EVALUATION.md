# Evaluation: Replacing libcurl with QNetworkAccessManager (QNAM)

## Overview
This document evaluates the feasibility, benefits, and costs of replacing the current `libcurl` based network layer with Qt's native `QNetworkAccessManager` (QNAM).

## Comparison

| Feature | libcurl (Current) | QNetworkAccessManager (Proposed) |
| :--- | :--- | :--- |
| **Dependencies** | Requires `libcurl` (external). | Built-in to `Qt5::Network`. |
| **Execution Model** | Synchronous / Blocking (fits ThreadPool). | Asynchronous / Signal-Slot (Event Loop based). |
| **Windows Build** | Harder. Needs separate DLLs/linking. | Easier. Standard Qt deployment. |
| **Performance** | High throughput, low overhead. | Good, but signal overhead on massive concurrency. |
| **Code Structure** | Linear, easy to follow in worker threads. | Event-driven, requires callback restructuring. |

## Impact Analysis

### 1. Architecture Refactor
The current `DownloadManager` uses a `ThreadPool` where each thread grabs a task and blocks until the download finishes:

```cpp
// Current Model
void worker_thread() {
    while(job = queue.pop()) {
        downloader.download(job.url); // Blocks
        // ... handle result
    }
}
```

QNAM requires an event loop to process network traffic. It cannot simply be dropped into the existing worker threads without spinning up a `QEventLoop` in each thread, which is inefficient. The ideal QNAM architecture is single-threaded (or few-threaded) asynchronous:

```cpp
// QNAM Model
void start_next_download() {
    if (active_downloads < max) {
        QNetworkReply* reply = qnam.get(request);
        connect(reply, &QNetworkReply::finished, this, &handle_finished);
    }
}

void handle_finished() {
    // ... write file, update DB
    start_next_download();
}
```

**Conclusion**: Switching to QNAM requires rewriting `DownloadManager` to remove the `ThreadPool` for downloads and instead manage a queue of active `QNetworkReply` objects.

### 2. Cookie Management
*   **Current**: Custom `CookieJar` + manual header injection into `libcurl`.
*   **QNAM**: Has `QNetworkCookieJar`. We would need to adapt the logic to subclass `QNetworkCookieJar` or sync our custom jar with it.

### 3. Database Concurrency
*   **Current**: Multiple threads write to DB. `sqlite3` in WAL mode handles this well.
*   **QNAM**: Callbacks would likely run on the main thread (or a dedicated network thread). Database writes might need to be offloaded to a separate thread to prevent blocking the network event loop, or we accept that DB writes happen on the network thread.

## Recommendation

**Stick with libcurl for now, unless Windows build complexity becomes a blocker.**

Reasons:
1.  **Stability**: The current threaded model is working and robust.
2.  **Effort**: The refactor is significant (estimated 1-2 days of work) and introduces regression risks.
3.  **Performance**: For brute-forcing millions of IDs, the low overhead of `libcurl` threads is advantageous.

## Alternative: "Qt-ifying" the Build Only
If the goal is simply to make Windows builds easier, we can use `vcpkg` (as documented in `BUILD_WINDOWS.md`) which makes installing `libcurl` on Windows trivial. This solves the dependency pain without requiring a code rewrite.
