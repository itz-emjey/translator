# Language Management System (C)

A standalone C implementation of the translation app. The Python version has
been removed, so this folder now contains only the C project.

It implements:

- Language pickers for `auto`, `ceb`, `en`, `fr`, and `tl` (source allows
  auto-detect, target does not).
- Translation through the same Google endpoint
  (`translate.googleapis.com/translate_a/single`, `client=dict-chrome-ex`).
- Detected-language reporting when the source is `auto`.
- Word "meaning" panel populated from Google's dictionary payload.
- The `POST /api/translate` JSON contract used by `templates/index.html`.

## Layout

```
include/          public headers
src/
  main.c          CLI entry point
  server.c        HTTP/1.1 server and routes
  translator.c    Google Translate client and meaning extraction
  http_client.c   HTTPS GET (WinHTTP on Windows, libcurl elsewhere)
  json.c          small JSON parser (arrays, objects, strings, numbers)
  languages.c     language table
  util.c          string buffer, URL encoding, file helpers
templates/
  index.html      UI, with language-option placeholders
Makefile          POSIX / MinGW build
build-mingw.bat   Windows build with gcc
build-msvc.bat    Windows build with MSVC
translator.exe    prebuilt Windows binary
```

## Build

### Windows (no third-party libraries)

The Windows build uses WinHTTP, which ships with Windows.

With MinGW-w64 (for example `w64devkit`):

```bat
build-mingw.bat
```

or:

```bat
gcc -std=c11 -O2 -Wall -Wextra -Iinclude src\*.c -o translator.exe -lwinhttp -lws2_32
```

With Visual Studio (from a Developer Command Prompt):

```bat
build-msvc.bat
```

### Linux / macOS

The POSIX build uses libcurl. Install the development package first
(`libcurl4-openssl-dev` on Debian/Ubuntu, `curl` with Homebrew on macOS), then:

```sh
make
```

## Run

```bat
translator.exe
```

Then open <http://127.0.0.1:5000>.

Options:

```
translator [--host ADDRESS] [--port PORT]
```

Defaults are `127.0.0.1` and `5000` (the Flask default). Use
`--host 0.0.0.0` to expose the server on the local network.

## API

`POST /api/translate`

Request:

```json
{ "text": "hello", "source": "en", "target": "fr" }
```

Response:

```json
{
  "translated": "Bonjour",
  "detected": "en",
  "meaning": "\u2022 Hello!  (interjection)\n    - Bonjour!\n    - Salut!"
}
```

`meaning` is an empty string unless Google returns dictionary data (normally
only for single words).

Errors return a non-200 status and:

```json
{ "error": "Unsupported target language: xx" }
```

## Behavior notes

- The server is intentionally single-threaded and handles one request at a
  time. That is fine for local use; a slow upstream request briefly queues
  other requests.
- HTTPS is provided by WinHTTP on Windows and by libcurl on POSIX; the C code
  never implements TLS itself.
- Requests are capped at 1 MiB total and 256 KiB of body.
- The page template is loaded from `templates/index.html` relative to the
  working directory.
- Only the five languages from the original language table are accepted.
