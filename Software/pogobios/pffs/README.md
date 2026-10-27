# PFFS v4 source provenance

The flash-file and flash-log sources and headers are copied from
`pogo-utils` commit `ac6d558`, under `src/pogo-utils/`. The UART protocol in
`transfer.c/.h` comes from that commit's `examples/flash_file/`. The host client
is copied to `Software/pffs_transfer.py`. These are repository-local copies;
building Pogobios does not require a neighboring checkout or Pogosim.

The flash modules include `pogobot.h` directly in place of the Pogosim
`pogobase.h` wrapper. The defrag selection object is zero-initialized to keep
its no-next sentinel explicit. The writer requires `REAL_ROBOT`, selected by
the BIOS Makefile, so simulator erase emulation cannot enter a firmware build.
The v4 catalog format, page geometry and UART
wire protocol are unchanged. The library's create functions retain their
original autoformat behavior; BIOS command and transfer entry points check
catalogs first so only `format YES` can initialize or replace catalogs.

`../cmd_pffs.c` adapts the example's file operations to native BIOS handlers,
a shared page cache and incremental service calls. `../console_input.c`
serializes file sessions against other console commands and prohibits PFFS
over IR. The local LiteX `readline.c`, `readline_simple.c` and `readline.h`
retain their original notices and add bounded 160-byte input plus file-service
hooks. Completion is compiled with that same header.

See [PFFS file commands](../../readme.md#pffs-file-commands) for usage and
persistence limits. Native NOR and protocol tests are adapted from the same
pogo-utils revision and supplemented by BIOS console integration tests.
