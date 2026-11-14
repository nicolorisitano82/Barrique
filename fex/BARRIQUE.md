# FEX for Barrique

This folder is a copy of [FEX](https://github.com/FEX-Emu/FEX) taken at commit
`ab135df`, with the source of the submodules the Windows build needs
(`External/fmt`, `External/range-v3`, `External/rpmalloc`,
`External/unordered_dense`, `External/xxhash`, `Source/Common/cpp-optparse`).
The other submodules (tests, Vulkan/DRM headers, vixl, tracy, zydis, jemalloc,
Catch2) are not included.

FEX is MIT-licensed (see `LICENSE`); the submodules keep their own licenses.

## Changes from upstream

The diff against upstream is also kept as `../patches/fex/0001-barrique-wow64-low-window.patch`.

- `Source/Windows/WOW64/Module.cpp`: `ToGuestAddress`/`ToHostAddress` for the
  32-bit address space kept in a window above 4GB on macOS (Wine's
  Hypervisor.framework backend), applied to the main module, the x86 ntdll,
  the trampolines and unix call arguments.
- `Source/Windows/Common/InvalidationTracker.*`: unmapping an address that is
  not a mapping no longer drops every executable range below it; the DEP
  change scan and the section lookup convert window addresses to 32-bit ones.

## Not for upstream

The FEX project does not accept AI-generated code (see `CLAUDE.md` and
`AGENTS.md`, kept as they are). The changes in this folder were written with
an AI assistant for Barrique's own builds. They must not be submitted to FEX
as pull requests or patches. Problems found in FEX can be reported to them as
plain bug descriptions.
