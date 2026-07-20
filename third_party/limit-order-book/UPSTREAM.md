# Vendored limit-order-book subset

This directory contains the minimal C++ matching-book sources required by LOBX:

- `cpp/include/lob/*.hpp`
- `cpp/src/book_core.cpp`
- `cpp/src/price_levels.cpp`

Upstream project:

- Repository: https://github.com/mansoor-mamnoon/limit-order-book
- Source commit: `78e1fb0e0563388456e5030d858ef43d6407bed3`

The upstream README identifies the project as MIT-licensed. Before publishing a
public redistribution, verify the upstream license/copyright notice and include
the exact upstream license text required for redistribution.

The CMake cache variable `LOB_REPO` can point to a full external checkout instead:

```bash
cmake -S . -B build -DLOB_REPO=/path/to/limit-order-book
```
