# 🥣 Chex Quest PSP

A port of **Chex Quest** (1996) to the **PlayStation Portable**, built on top of [doomgeneric](https://github.com/ozkl/doomgeneric) with a fully custom PSP platform layer.

Includes hardware-accelerated rendering via GU, full controller mapping, OPL2 FM music synthesis, and sound effects — all running natively on the PSP.

![Platform](https://img.shields.io/badge/platform-PSP-blue)
![Build](https://img.shields.io/github/actions/workflow/status/DraxTube/Chex-Quest-PSP/build.yml?branch=main)
![License](https://img.shields.io/badge/license-GPL--2.0-green)

### 🔥 ALSO AVAILABLE ON PSVITA!
**I have just released a native port for the PlayStation Vita!**
It features a custom OPL2 synthesizer, native resolution, and full optimization for the PSVITA hardware.
👉 **[Check out the Chex Quest PSP Port here](https://github.com/DraxTube/chexquest-vita)**
---

## 📸 Screenshots

> *Coming soon*

---

## ✨ Features

- **Hardware-accelerated rendering** using `sceGu` (PSP GPU)
- **320×200 internal resolution** scaled to 480×272 (PSP native)
- **Full OPL2 FM synthesizer** — software emulation with sine/exp lookup tables, ADSR envelopes, 9 OPL channels
- **GENMIDI instrument loading** directly from the WAD file
- **MUS to MIDI conversion** with full sequencer (tempo changes, looping, multi-track)
- **8-channel SFX mixing** with per-channel volume and stereo panning
- **Analog stick support** for smooth movement and turning
- **Weapon cycling** via D-pad with time-based key release
- **Quick Save / Quick Load** mapped to D-pad
- **Custom XMB assets** — ICON0.PNG and PIC0.PNG embedded in EBOOT.PBP
- **Automatic WAD detection** — searches multiple paths on the Memory Stick
- **Debug logging** to `debug.txt` for troubleshooting
- **CPU clocked to 333 MHz** for maximum performance

---

## 🎮 Controls

| PSP Button | Action |
|---|---|
| **Analog Stick** | Move forward/backward, turn left/right |
| **✕ (Cross)** | Fire / Confirm / Yes |
| **○ (Circle)** | Use (open doors, activate switches) |
| **□ (Square)** | Toggle Automap |
| **△ (Triangle)** | Run (hold) |
| **L Trigger** | Strafe left |
| **R Trigger** | Strafe right |
| **D-Pad Left** | Previous weapon |
| **D-Pad Right** | Next weapon |
| **D-Pad Up** | Quick Save (F6) |
| **D-Pad Down** | Quick Load (F9) |
| **Start** | Menu (Escape) |
| **Select** | Confirm (Enter) |

---

## 📦 Installation

### Requirements

- A **PSP** with Custom Firmware (CFW) — e.g., PRO-C, ME, ARK, Infinity
- The **Chex Quest WAD file** (`chex.wad`) — freely available, it was originally distributed as a cereal box promotional game

### Steps

1. Download `ChexQuest-PSP.zip` from the [Releases](../../releases) page or from [Actions](../../actions) artifacts
2. Extract the `ChexQuest` folder
3. Copy it to your Memory Stick:
5. Launch **Chex Quest** from the PSP XMB Game menu

### WAD Search Paths

The game searches for the WAD file in this order:

1. `ms0:/PSP/GAME/ChexQuest/chex.wad`
2. `ms0:/PSP/GAME/chexquest/chex.wad`
3. `./chex.wad`
4. `chex.wad`

If the WAD is not found, the game exits after 3 seconds. Check `debug.txt` for details.

