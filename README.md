# Chex Quest PSP

Port of **Chex Quest** (1996 cereal box FPS) to Sony PSP using
[doomgeneric](https://github.com/ozkl/doomgeneric).

Chex Quest runs on the Doom engine, so this is a Doom port with
PSP hardware acceleration via GU, configured to load `chex.wad`.

## Building

Builds automatically via GitHub Actions. Just push to `main`.

### Manual build (needs pspdev toolchain):

```bash
git clone https://github.com/ozkl/doomgeneric.git
cp doomgeneric_psp.c doomgeneric/doomgeneric/
cp Makefile doomgeneric/doomgeneric/
cd doomgeneric/doomgeneric
make
