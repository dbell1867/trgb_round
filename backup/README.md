# Factory firmware backup — LilyGO T-RGB 2.8" (GT911)

- `factory.bin` — full 16 MB flash dump, taken 2026-10-11, read-only.
- SHA-256: see `factory.bin.sha256` (`70fc7bf1…63fe8f`).
- Confirmed by the chip: `esptool verify-flash 0 factory.bin` → "digest matched".

## Restore
```bash
sha256sum -c backup/factory.bin.sha256
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 backup/factory.bin
```
If the port won't connect: hold BOOT, tap RST, release BOOT, then retry.

## How it was taken (and why not one command)
`esptool read-flash 0 ALL` failed with "Packet content transfer stopped" at
0x1b2000. The fast stub reader stalls on fixed, content-dependent blocks over the
S3's native USB-Serial/JTAG (skill gotcha G42). `scripts/backup_flash.py` reads in
1 MB chunks, drops to 64 KB, then 4 KB ROM-reader (`--no-stub`) reads for failing
pieces. Stub failed at 64 KB pieces 0x1b0000 and 0x210000 (both inside app0);
the ROM reader read all 32 blocks there. Total time ~3 min.

## Factory partition table
| name | type | offset | size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 20 KB |
| otadata | data/ota | 0xe000 | 8 KB |
| app0 | app/ota_0 | 0x10000 | 6400 KB |
| app1 | app/ota_1 | 0x650000 | 6400 KB |
| spiffs | data/spiffs | 0xc90000 | 3456 KB |
| coredump | data/coredump | 0xff0000 | 64 KB |
