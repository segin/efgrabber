# Epstein Files Grabber (efgrabber)

A high-performance multithreaded downloader and scraper for the Epstein Files released by the U.S. Department of Justice.

## Features

- **Scraper Mode**: Parses index pages from justice.gov to discover and download PDF files
- **Brute Force Mode**: Iterates through all possible file IDs in the EFTA numbering scheme
- **Hybrid Mode**: Combines both scraper and brute force approaches
- **High Performance**: Up to 1000 concurrent downloads with configurable limits
- **Resume Support**: SQLite database tracks progress; safely interrupt and resume
- **All Data Sets**: Supports Data Sets 1-12 with automatic page count detection
- **Qt5 GUI**: Modern graphical interface showing real-time progress
- **CLI Version**: Headless operation for servers and automation

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+)
- CMake 3.20+
- Qt5 Widgets (for GUI version)
- libcurl
- SQLite3

### Ubuntu/Debian
```bash
sudo apt install build-essential cmake qtbase5-dev libcurl4-openssl-dev libsqlite3-dev
```

### Fedora
```bash
sudo dnf install gcc-c++ cmake qt5-qtbase-devel libcurl-devel sqlite-devel
```

### Arch Linux
```bash
sudo pacman -S base-devel cmake qt5-base curl sqlite
```

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This produces two executables:
- `efgrabber` - Qt5 GUI application
- `efgrabber-cli` - Command-line interface

## Usage

### GUI Application

```bash
./efgrabber
```

The GUI provides:
- Data set selection (1-12)
- Mode selection (Scraper/Brute Force/Hybrid)
- Real-time progress bars for overall, scraper, and brute force progress
- Statistics display (completed, failed, pending, speed, etc.)
- Log view with timestamped messages
- Start/Pause/Stop controls

### CLI Application

```bash
./efgrabber-cli [OPTIONS]
```

Options:
- `-d, --data-set N` - Data set number (1-12, default: 11)
- `-m, --mode MODE` - Mode: scraper, brute, hybrid (default: scraper)
- `-o, --output DIR` - Output directory (default: downloads)
- `-c, --concurrent N` - Max concurrent downloads (default: 1000)
- `-r, --retries N` - Max retry attempts (default: 3)
- `-s, --start ID` - Brute force start ID
- `-e, --end ID` - Brute force end ID

Examples:
```bash
# Scrape Data Set 11
./efgrabber-cli -d 11 -m scraper

# Brute force Data Set 11 with custom range
./efgrabber-cli -d 11 -m brute -s 2205655 -e 2730262

# Hybrid mode for Data Set 9 with reduced concurrency
./efgrabber-cli -d 9 -m hybrid -c 500
```

## How It Works

### Cookie Authentication

The DOJ website requires the `justiceGovAgeVerified=true` cookie to be set. This is automatically handled by the downloader.

### File Organization

Downloaded PDFs are organized in subdirectories to avoid filesystem limitations:
```
downloads/
├── DataSet11/
│   ├── 022/
│   │   ├── EFTA02205655.pdf
│   │   ├── EFTA02205656.pdf
│   │   └── ...
│   ├── 023/
│   │   └── ...
│   └── ...
└── DataSet9/
    └── ...
```

### Database Schema

Progress is tracked in an SQLite database (`efgrabber.db`):

- `files` - Individual file records with status (PENDING, IN_PROGRESS, COMPLETED, FAILED, NOT_FOUND)
- `pages` - Index page scraping status
- `progress` - Brute force position for resume support

### Error Handling

- **404 errors**: Marked as NOT_FOUND and skipped (expected for non-existent file IDs)
- **Other errors**: Retried up to 3 times (configurable) before marked as FAILED
- **Interruption**: Press Ctrl+C for graceful shutdown; progress is saved

## Data Set Information

| Data Set | Description |
|----------|-------------|
| 1-8 | Earlier releases |
| 9 | Large document collection (~9308 pages) |
| 10 | Additional documents |
| 11 | Known range: EFTA02205655 - EFTA02730262 (~6538 pages) |
| 12 | Most recent release |

Page counts are auto-detected at runtime and may change as the DOJ adds or removes documents.

## License

This software is for educational and research purposes. Ensure compliance with applicable laws and the DOJ website's terms of service.
