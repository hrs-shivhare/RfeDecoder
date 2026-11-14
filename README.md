# RFE Decoder 1.0

Portable Windows utility for extracted EA Sports Cricket 07 `.rfe` files.

## Run

Distribute **RfeDecoder.exe only**. It needs no Python, .NET, Java, game
installation, Visual C++ redistributable, or companion DLLs. Windows 10/11
64-bit is the supported platform; it uses built-in Windows system libraries.

- **Single file:** Browse to an RFE, select **Convert file**, then choose its
  destination in **Save As**. The original input is protected. Saving over a
  different existing output requires confirmation in the Windows dialog.
- **Folder:** Browse to a folder and select **Convert folder**. Results are
  saved as `selected-folder\decoded\original-name.rfe`.
- **Include subfolders:** Optional; preserves the relative folder structure.
  Directories named `decoded` and directory links/junctions are excluded.
- Existing batch outputs are skipped, never overwritten. Unsupported or
  damaged RFEs are reported individually; other files continue converting.

## Formats

The app detects standard `10 FB` RefPack compression, decompresses if needed,
decodes `00 BE EF FF` HBXML, preserves element text and attributes, serializes
US-ASCII XML with character references, and verifies the output structure.
Extended HBXML values are supported. Binary output retains the `.rfe` extension.

Already readable RFEs are structurally checked and copied byte-for-byte,
preserving the native loose attribute syntax, duplicate attributes, encoding
declarations, and comments. This is intentionally not a strict XML validator
or a formatter for existing text. Empty files, unclosed/mismatched tags,
non-Screen roots, DTDs, invalid references, cycles and truncated compression
are rejected. UTF-16 text is not supported. Input and decoded-output safety
limit: 64 MB per file.

Extract the RFE from its BIG archive before using this app. It does not browse
or rebuild BIG/GOB archives and does not install files into the game.

## Optional command line

The same EXE also supports automation without opening its window:

```text
RfeDecoder.exe --convert input.rfe output.rfe
RfeDecoder.exe --batch "C:\ModWork\RFEs"
RfeDecoder.exe --batch "C:\ModWork\RFEs" --recursive
```

Command-line outputs never overwrite existing files. Exit codes: 0 success,
1 input/global failure, 2 batch completed with one or more file failures.

## Build from source

Development only: run `build.ps1 -Zig C:\path\to\zig.exe` with Zig 0.15.2.
The GUI, decoder and C++ support are linked into one EXE; the build also
creates a separate development test driver. No test files are needed to run
the distributed EXE.

## Validation

Independent reconstruction of all 280 installed RFE members found 275 valid
files (253 HBXML, 22 text), four empty files and one unclosed plaintext credits
file. All 275 valid files converted. All 14 compressed members were covered.
Binary comparisons include element names, attributes and non-whitespace body
content, including six elements omitted by the older Python helper.
Additional tests cover extended values, Latin-1 body text, overlapping RefPack
back-references, malformed tables, cycles, truncation and original/output
protection. Tests use workspace copies and leave game resources unchanged.

This is a decoder/editor-preparation utility. It does not prove that subsequent
edits to screen IDs, widget structure or event names are valid in the game.
