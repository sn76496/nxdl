# Contributing to NXDL

Contributions are provided under MPL-2.0; preserve the license notices and
acknowledgments. See NOTICE.md for the project's licensing and upstream context.

## Source style

NXDL follows the style of CTRDL's C sources:

- Four spaces per indentation level, attached opening braces, indented cases.
- Pointer stars aligned to the type (`const char* name`).
- Spaces around operators and after commas; no space after C casts.
- Separate statements and expanded control flow instead of compressed lines.
- Blank lines between functions and logical sections; short guard clauses may
  omit braces, while multi-statement branches use braces.
- Internal helpers use the `nxdl_` prefix and camelCase; globals use `g_`.
  Keep existing public names stable.

The checked-in .clang-format approximates the upstream style; CTRDL does not
supply a formatter configuration. Use clang-format 19.1.7 for reproducible results:

```sh
make format
make format-check
```

A formatter is only required when formatting or checking style, not to build or
install the library. Override `CLANG_FORMAT` to select its executable.

## Validation

```sh
make all smoke
make check
make install DESTDIR=/tmp/nxdl-stage
```

Host checks validate parsing and relocation with sanitizers. Console tests must
remain independent programs that print results to the Switch screen and also
save the same results to an output file. Do not introduce the previous runner.
A style-only change does not establish a new console execution result; keep
historical hardware logs distinct from freshly verified behavior.
