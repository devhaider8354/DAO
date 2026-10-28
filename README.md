# DAO Download Manager

[![Build](https://github.com/devhaider8354/DAO/actions/workflows/build.yml/badge.svg)](https://github.com/devhaider8354/DAO/actions/workflows/build.yml)

**English** | [العربية](README.ar.md)

DAO is a fast, lightweight download manager for Windows written in plain C on top of the Win32 API and WinHTTP. It splits every file into segments, downloads them over multiple connections at once, resumes interrupted downloads and integrates with Chrome, Edge and Brave.

<!-- ![DAO](docs/screenshot.png) -->

## Features

- **Multi-connection downloads** – up to 32 parallel connections per file using HTTP range requests.
- **Dynamic segment splitting** – when a connection finishes its part it takes over half of the largest remaining segment, so every connection stays busy until the very end.
- **Pause & resume** – progress is saved every two seconds. Downloads survive app restarts, network drops and reboots.
- **Automatic retries** with back-off on connection errors.
- **Browser integration** – a Chrome extension hands downloads over to DAO, including cookies and referrer, so downloads that need a logged-in session still work.
- **New download dialog** – file name, size, resume support, target folder and connection count before you start.
- **Clipboard monitoring** – copy a link and it appears in DAO automatically.
- **Segment map** – a live view of every part of the file as it downloads.
- **English and Arabic UI** with full right-to-left layout.
- **Dark modern UI**, high-DPI aware.
- **No dependencies** – only Windows system libraries. Small single executables.
- Smart file naming: `Content-Disposition`, Unicode names, names embedded in tokenized (JWT) links, MIME-based extensions and long-name handling.

## Download

Grab `DAO-Setup.exe` from the [Releases](../../releases) page and run it. The installer:

- installs DAO into `Program Files\DAO`
- creates Start menu and desktop shortcuts
- registers the browser bridge for Chrome, Edge and Brave
- adds DAO to *Settings → Apps* with a full uninstaller

Requires Windows 10 or 11 (x64).

## Chrome integration

The extension lives in the [`extension`](extension) folder.

**Option 1 – load it yourself (one-time, under a minute)**

1. Open `chrome://extensions` and turn on **Developer mode**.
2. Click **Load unpacked** and choose `C:\Program Files\DAO\chrome-extension`.
3. Restart Chrome.

DAO opens a step-by-step guide for this on first launch (button **Connect Chrome**), and turns green as soon as the extension connects.

**Option 2 – Chrome Web Store**

Chrome only lets desktop installers add extensions that are published on the Chrome Web Store (this is how other download managers do it). After publishing, build the installer with the store ID and Chrome will simply ask the user to *Enable* the extension:

```
make STORE_ID=<your-extension-id>
```

Once installed, every download started in the browser opens DAO's *New download* window instead of Chrome's downloader. You can also right-click any link and choose **Download with DAO**. The extension popup lets you pause capturing or skip small files, specific file types or sites. If DAO is not available, Chrome continues the download itself.

## Usage

| Action | How |
| --- | --- |
| Add a download | Paste a link and press **Enter** or **Add download** |
| Pause / resume | **Space**, toolbar buttons or right-click menu |
| Delete | **Delete** (Shift+Delete also removes the file) |
| Open a finished file | Double-click or **Enter** |
| Switch language | Header button (English / عربي) |

Settings and the download list are stored in `%APPDATA%\DAO`.

### Command line

`dao-cli.exe` is a standalone console version of the engine:

```
dao-cli "https://example.com/file.zip"
dao-cli "https://example.com/file.zip" -n 16 -o file.zip -d D:\Downloads
```

Press `Ctrl+C` to pause and run the same command again to resume.

## Building from source

**MSYS2 / MinGW-w64**

```
pacman -S mingw-w64-ucrt-x86_64-gcc make
make
```

**Cross-compiling from Linux** (e.g. [llvm-mingw](https://github.com/mstorsjo/llvm-mingw))

```
make CROSS=x86_64-w64-mingw32-
```

**Visual Studio** – run `build.bat` from a *Developer Command Prompt*.

Output goes to `build/`: `dao.exe`, `dao-host.exe`, `dao-cli.exe` and `DAO-Setup.exe`.

## Project structure

```
src/
  main.c        Win32 UI: main window, download list, dialogs, Chrome setup guide
  engine.c/.h   download engine: probing, segments, workers, resume state
  i18n.c/.h     English / Arabic string table
  host.c        native messaging host (bridge between the extension and DAO)
  cli.c         command-line downloader
res/            icon, manifest and version resources
installer/      installer and uninstaller (files are embedded as resources)
extension/      Chrome extension (Manifest V3, English and Arabic)
```

## How it works

1. **Probe** – a `Range: bytes=0-0` request reveals the file size, range support and the file name.
2. **Split** – the file is pre-allocated as `name.part` and divided into equal segments, one per connection.
3. **Download** – each worker thread requests its byte range and writes at its own offset. When a worker runs out of work, it splits the largest active segment in half and takes the second half.
4. **Resume** – the remaining ranges are saved to `name.dao` every two seconds. On resume, only the missing ranges are requested.
5. **Finish** – the `.part` file is renamed to the final name and the state file is removed.

Browser flow: the extension pauses Chrome's download and sends the URL, cookies and referrer to `dao-host.exe` through Chrome Native Messaging. The host starts DAO if needed and passes the request with `WM_COPYDATA`, then the extension cancels Chrome's copy. If anything fails, Chrome resumes its own download.

## Contributing

Issues and pull requests are welcome. Please keep the code dependency-free (Win32 + WinHTTP only) and add new UI text to `src/i18n.c` in both languages.

## License

[MIT](LICENSE) © 2026 FLOW POINT – [flowp.uk](https://flowp.uk)
