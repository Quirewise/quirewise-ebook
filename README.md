# quirewise-ebook

Standalone C11 ebook reader package.

CMake target: `Quirewise::Ebook`. Public headers are under `include/quirewise/`.

## Capabilities

- **EPUB/KEPUB**: reads the OCF package and chapter XHTML, producing
  sections/paragraphs/runs with basic styles and links. DRM is unsupported.
- **MOBI 7 / AZW3 / AZW**: parses the PalmDB/PalmDOC container and returns
  plain text for uncompressed and PalmDOC-compressed records (CP1252 or UTF-8).
  HUFF/CDIC compression, DRM, embedded resources and fixed layouts are
  unsupported; text is flattened to paragraphs.
- **FB2**: FictionBook 2 XML with headings, paragraphs, emphasis, quotes,
  poems, links and inline JPEG/PNG/GIF images.
- **PML**: strips the supported basic PML commands into text.
- **HTMLZ**: extracts and returns the primary HTML/XHTML member.
- **TXTZ / PMLZ**: extracts the primary text member (index-first).

## Limits

All readers are **read-only**: there is no EPUB/DOCX/MOBI writer in this package
(the "PDF emission" helpers intentionally do not exist here; the document
package owns PDF output). Per-format size/entry limits are enforced and return
`QW_EBOOK_ERR_RESOURCE_LIMIT`. MOBI text is capped at 32 MiB and markup is
flattened. CHM, LIT, LRF, PDB, RB, SNB and TCR are not implemented.

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build
```
