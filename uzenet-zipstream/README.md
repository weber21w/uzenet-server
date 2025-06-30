# uzenet-zipstream

`uzenet-zipstream` is a minimal TCP server that enables HTTP streaming of a single file from within a ZIP archive. It is designed for constrained clients like Uzebox, allowing efficient and stateless decompression over a socket without any disk I/O.

## Protocol Overview

- Listens on TCP port **38030**.
- Expects the client to send:

  ```
  Unzip http://example.com/path/to/file.zip\n
  ```

- Responds with:
  - A 4-byte big-endian (network order) unsigned integer representing the **uncompressed size** of the first entry.
  - The **raw decompressed data** of the first file entry in the ZIP archive.

## Features

- No file writes or HTTP headers.
- All processing is in-memory.
- Fully supports both Windows and Unix/Linux platforms.
- Automatically decompresses ZIP entries using miniz (tinfl).
- Robust error handling and syslog support (on Linux).
- Uses `libcurl` for fetching only the necessary ZIP sections via HTTP Range requests.

## Usage

The client must:
1. Connect via TCP to port 38030.
2. Send an `Unzip` command with a valid HTTP(S) URL.
3. Read the 4-byte uncompressed size.
4. Receive the streamed decompressed contents of the first file in the archive.

## Dependencies

- `libcurl`
- `miniz` (bundled)
- Standard C networking APIs
- `pthread` (on Unix)
- `ws2_32` (on Windows)

## Example Client Logic

```c
send(sock, "Unzip http://example.com/file.zip\n", ...);
read(sock, &len, 4);         // get uncompressed length
read(sock, buffer, len);     // read uncompressed data
```

## Use Cases

- Remote file streaming to microcontrollers.
- Static content broadcasting from a pre-zipped archive.
- Instant unpacking of remote resources into RAM on client devices.

## Security Notes

- Only the first file in the ZIP archive is decompressed.
- No authentication is performed by default.
- Future extensions could restrict URLs or validate source IPs.

## Logging

- Uses `syslog()` on Linux to log connections, timeouts, and errors.

## License

MIT or public domain, at your discretion.
