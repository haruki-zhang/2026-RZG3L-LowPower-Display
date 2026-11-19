# RZ/G3L LowPower Display

## 1. Overview

This package provides a proof-of-concept (PoC) for the **RZ/G3L SMARC
Evaluation Board Kit**, based on Renesas RZ Linux BSP Plus for RZ/G3L: **the
display stays on while Linux is in system suspend**. It is a set of Yocto layers added to the
standard BSP build.

### Objective

With the standard BSP, system suspend turns the SoC off through the PMIC, and
the display goes dark. The PoC shows a low power state in which:

- **The screen stays on.** The CA55 (Cortex-A55) sleeps and the DDR is in
  self-refresh. The display shows a low power picture from on-chip SRAM.
- **The CM33 (Cortex-M33) runs the display.** It hands the running display over from
  Linux and back, with no black frame and no flicker.
- **The power is shown live.** The CM33 measures the SMARC module power and
  updates the reading on the low power picture every second.
- **One button returns.** USER_SW3 wakes the system. The picture returns to
  the Linux GUI at once, before Linux has resumed.
- **No changes to the BSP.** Everything is added as Yocto layers.

### At a Glance

```text
┌─────────────────────────┐   Enter Low Power   ┌─────────────────────────┐
│      NORMAL SCREEN      │ ──────────────────▶ │    LOW POWER SCREEN     │
│                         │                     │                         │
│  Linux GUI on Weston    │                     │  Picture from SRAM      │
│  Live power, 10 min     │                     │  Live power, saving %   │
│  chart, button          │      USER_SW3       │  Drawn by the CM33      │
│                         │ ◀────────────────── │                         │
│  CA55 running, DDR on   │                     │  CA55 asleep,           │
│                         │                     │  DDR in self-refresh    │
└─────────────────────────┘                     └─────────────────────────┘
```

| | **Normal screen** | **Low power screen** |
|---|---|---|
| Drawn by | `lpdemo-gui`, CA55 | CM33 firmware |
| Frame buffer | DDR | On-chip SRAM |
| Content | Current power, last 10 minutes as a chart, "Enter Low Power" button | "LOW POWER MODE", current power, saving against the normal mode |
| CA55 | Running Linux | Asleep (WFI in SRAM) |
| DDR | Active | Self-refresh |

This low power state is called **DISP_RET** in this guide.

> **Notice:** This guide assumes the build environment of Renesas RZ Linux
> BSP Plus ("BSP Plus"), `https://renesas-rz.github.io/rz_linux_bsp_plus/`,
> RZ/G Series. It describes only the additional steps. References such as
> "BSP Plus Getting Started, Step 4" point to the RZ/G3L tab of that page.

---

## 2. Environment

### Hardware Environment

| **Component** | **Description** |
|---|---|
| **Board** | RZ/G3L SMARC Evaluation Board Kit (`smarc-rzg3l`)|
| **Display** | HDMI monitor with 1920x1080@60 |
| **Input** | USB mouse, or a USB touch monitor |
| **Wake-up button** | USER_SW3 on the carrier board |
| **Power, serial cable, microSD card** | BSP Plus Getting Started, Step 2 |
| **Host Machine** | Linux PC (Ubuntu 22.04) |

The power is measured by the ISL28022 on the carrier board. No additional
hardware is needed.

### Software Environment

| **Software** | **Version / Description** |
|---|---|
| **Linux BSP** | Renesas RZ Linux BSP Plus for RZ/G3L |
| **Graphics package** | RZ Graphics Library Package V5.1.3.2 (BSP Plus How To Build, Step 1) |
| **Multi-OS package** | `meta-rz-multi-os` (BSP Plus How To Build, Step 3) |
| **Target Machine** | `smarc-rzg3l` |
| **Image** | `lpdemo-image` |

---

## 3. How It Works

### 3.1 Key Components

| Layer | Function |
|---|---|
| `meta-rzg3l-seamless-logo-still` | U-Boot starts the display and shows the boot logo. The kernel takes over the running display instead of resetting it |
| `meta-rzg3l-lp-display` | The LowPower Display, below |

`meta-rzg3l-lp-display` changes:

- **CM33 firmware** (bare metal, runs only from SRAM). BL2 starts it at
  power-on. It measures the module power, hands the display over between
  Linux and itself, redraws the power reading, and wakes the system on
  USER_SW3.
- **TF-A**: a new system suspend mode (`PLAT_SYSTEM_SUSPEND=disp`) replaces
  the BSP default (`vbat`, SoC power-off through the PMIC). The CA55
  sleeps in SRAM with the DDR in self-refresh.
- **U-Boot**: the display starts at 1920x1080.
- **Kernel**: the display drivers hand the display over to the CM33 on
  suspend and take it back on resume, without stopping it. RIIC0 is left to
  the CM33. The shared SRAM is exposed to Linux.
- **Applications**: `lpdemo-gui` (the Normal screen, started at boot) and
  `lpdemo-ctl` (command-line tool, Section 5.3).

### 3.2 Display Hand-over

The CM33 does not reprogram the display. It only switches the display list
that the video signal processor (VSPD) reads at every frame start:

- **To the CM33**: the VSPD is pointed to a display list in SRAM. The switch
  takes effect at the next frame start, so it never tears.
- **Back to Linux**: the VSPD is pointed back to the Linux display list. The
  Linux frame buffer in DDR is unchanged, so the GUI is shown exactly as
  before.

The low power picture is composed of three layers, all in SRAM:

| Layer | Size and position | Content |
|---|---|---|
| Background | Full screen | Solid colour (no memory read) |
| RPF0 | 720x88 at (600, 436) | "LOW POWER MODE", "Press USER_SW3 to return to normal mode" |
| RPF1 | 720x88 at (600, 556) | Power in W and the saving in %. The CM33 redraws the digits |

### 3.3 Low Power Entry and Wake-up

| # | By | Action |
|---|---|---|
| 1 | Linux | "Enter Low Power": writes the low power picture to SRAM and starts the system suspend |
| 2 | Linux | The display driver hands the display over to the CM33. The low power picture is on screen |
| 3 | TF-A | Puts the DDR into self-refresh. The CA55 sleeps in SRAM |
| — | CM33 | **DISP_RET**: keeps the picture on screen, measures and redraws the power every second |
| 4 | CM33 | USER_SW3: takes the DDR out of self-refresh and gives the display back to Linux. The GUI is on screen again |
| 5 | CM33 | Wakes the CA55 (MHU interrupt) |
| 6 | Linux | Resumes and takes the display back. The GUI runs again |

Measured on the EVK:

| Step | Time |
|---|---|
| Display hand-over to the CM33 | 13.8 ms (within one frame) |
| Display back to Linux | 31.0 ms |
| DDR out of self-refresh | 2.0 ms |
| Linux resume | about 0.9 s |

### 3.4 Power Measurement

- **What is measured:** the 5 V input of the whole SMARC module
  (`5V_MODULE`). It includes the PMIC losses, LPDDR4, eMMC and the other
  devices on the module. It does not include the carrier board or the
  monitor.
- **Normal screen:** the mean of the last 4 samples (one every 500 ms). The
  chart takes one point every 2 s.
- **Low power screen:** redrawn every second. The saving is calculated
  against the mean of the last 10 s before the low power entry. The CM33
  also stores one sample every 2 s; after the return, they are added to the
  chart.

### 3.5 SRAM Layout

| Address | Size | Content |
|---|---|---|
| `0x20000`–`0x59FFF` | 232 KiB | CM33 firmware (the top 1 KiB is reserved) |
| `0x5A000`–`0x5AFFF` | 4 KiB | DDR retention parameters |
| `0x5B000`–`0x5BFFF` | 4 KiB | Control block shared by Linux, CM33 and TF-A |
| `0x5C000`–`0x5DFFF` | 8 KiB | BL2 at boot, then the CM33 display lists |
| `0x5E000`–`0x7CFFF` | 124 KiB | RPF0 picture |
| `0x7D000`–`0x9BFFF` | 124 KiB | RPF1 picture |
| `0x9C000`–`0x9FFFF` | 16 KiB | TF-A (suspend code) |

---

## 4. Implementation Steps

#### Step 1. Prepare the Workspace

1. Prepare the BSP build environment for RZ/G3L (`BOARD=smarc-rzg3l`), with
   the Graphics package: BSP Plus How To Build, Step 1, and Step 2 items 1
   to 7.
2. Get the Multi-OS package and place it in the workspace: BSP Plus How To
   Build, Step 3, and the README of the package.
3. Copy both layers in `layers/` of this package into the top directory of
   the workspace.

#### Step 2. Add the Layers

```sh
# pwd should be the build directory of the workspace

bitbake-layers add-layer ../meta-rz-features/meta-rz-multi-os/meta-rzg3l
bitbake-layers add-layer ../meta-rzg3l-seamless-logo-still
bitbake-layers add-layer ../meta-rzg3l-lp-display
```

#### Step 3. Configure `local.conf`

Add to `build/conf/local.conf`:

```sh
USE_OPENAMP = "0"
```

Otherwise the Multi-OS package installs its own CM33 firmware. BL2 loads only
one.

#### Step 4. Build the Image

```sh
MACHINE=smarc-rzg3l bitbake lpdemo-image
```

The WIC image in `build/tmp/deploy/images/smarc-rzg3l/` contains `bl2`,
`fip`, the CM33 firmware and the rootfs.

#### Step 5. Write the Board for eSD Boot

1. **Write the WIC image to a microSD card** (BSP Plus Getting Started,
   Step 4):

   ```sh
   # pwd should be the build directory of the workspace
   # replace sdX with the actual microSD card device

   sudo bmaptool copy tmp/deploy/images/smarc-rzg3l/lpdemo-image-smarc-rzg3l.rootfs.wic.gz /dev/sdX
   ```

2. **Set eSD boot mode** (BSP Plus Appendix, How to boot from eSD):

   | DIP switch | 1 | 2 | 3 | 4 | 5 | 6 |
   |---|---|---|---|---|---|---|
   | `SW_MODE` (carrier board) | ON | ON | OFF | ON | | |
   | `SYS` (`SW_CONFIG`, module) | ON | OFF | ON | OFF | OFF | OFF |

3. **Insert the card into the microSD slot of the SMARC module** and power
   on: press and hold POWER for 1 second. The serial console is 115200 bps,
   8N1 (BSP Plus Getting Started, Step 5).

> **Note:** Always use `bl2`, `fip`, the CM33 firmware and the rootfs of the
> same build. The WIC image keeps them together.

### 4.1 Other Boot Media (Not Verified)

Write `bl2`, `fip` **and** the CM33 firmware of this build with Flash Writer
(`Flash_Writer_SCIF_RZG3L_SMARC_LPDDR4.mot`, BSP Plus Getting Started,
Step 5).

**eMMC boot** (BSP Plus Appendix, How to boot from eMMC): set `EXT_CSD` with
`EM_SECSD`, write the files below with `EM_W`, then write the rootfs with
`bmaptool`. For that, add `IMAGE_INSTALL:append = " bmaptool"` to
`local.conf` (BSP Plus How To Build, Step 2 item 8).

| File | Partition | Start sector | Program start address |
|---|---|---|---|
| `bl2_bp_mmc-smarc-rzg3l.srec` | 1 | `1` | `21e00` |
| `fip-smarc-rzg3l.srec` | 1 | `300` | `0` |
| `lpdemo-cm33-fw-smarc-rzg3l.srec` | 1 | `1380` | `0` |

**QSPI boot** (`XLS2`, BSP Plus Getting Started, Step 5). For booting, the
DIP switches and the microSD card with the rootfs are set as in BSP Plus
Getting Started, Step 6. Files to write:

| File | Program top address | QSPI save address |
|---|---|---|
| `bl2_bp_spi-smarc-rzg3l.srec` | `21e00` | `0` |
| `fip-smarc-rzg3l.srec` | `0` | `60000` |
| `lpdemo-cm33-fw-smarc-rzg3l.srec` | `0` | `270000` |

---

## 5. Running the Demo

### 5.1 Normal Screen

After power-on, the boot logo is shown until Weston starts. Then `lpdemo-gui`
starts full screen:

- **CURRENT POWER**: the module power in W.
- **Chart**: the last 10 minutes. Low power periods are shown as light blue
  bands.
- **Enter Low Power**: the button.

The value shows `-.---` for a few seconds after start-up, until the CM33 has
its first measurement.

### 5.2 Low Power and Return

1. **Click "Enter Low Power".** The screen turns dark and shows the low power
   picture. Linux is suspended; the serial console does not respond.
2. **Watch the reading.** The power and the saving are updated every second.
3. **Press USER_SW3.** The Normal screen returns at once, and the GUI runs
   again about 1 s later. The chart shows the low power period as a band,
   with the power measured in it.

### 5.3 Command-line Tool `lpdemo-ctl`

Log in on the serial console as `root`:

| Command | Function |
|---|---|
| `lpdemo-ctl status` | CM33 firmware version, measurement and low power state |
| `lpdemo-ctl watch` | Print each new measurement |
| `lpdemo-ctl suspend` | Low power, as with the button. Returns after USER_SW3 |
| `lpdemo-ctl suspend --wake-ms 5000` | Low power, wakes up after 5 s |
| `lpdemo-ctl suspend-cycle 100` | 100 automatic low power cycles |
| `lpdemo-ctl lp run` | Display hand-over only, Linux keeps running. Returns after USER_SW3 |

Example of `lpdemo-ctl status`:

```text
CM33 firmware   : 1.3.6 (clock 200.0 MHz)
CM33 state      : RUN  heartbeat ...  uptime ... s
RIIC0 grant     : granted
measurement     : OK  samples N  errors 0
ISL28022        : addr 0x44  config 0x0fff  Rshunt 8.000 mOhm
5V_MODULE       :  5.0xx V   xxx.x mA  ...
power           :  x.xxx W  (mean of 4: x.xxx W)
```

---

## 6. Known Limitations

- Verified on the RZ/G3L SMARC EVK with eSD boot. eMMC and QSPI boot are not
  verified.
- The power shown is the SMARC module 5 V input. The saving on the screen is
  smaller than the saving of the SoC supplies alone.
- The low power picture is limited to two 720x88 layers on a solid
  background. A full-screen picture does not fit in the on-chip SRAM.
- After the return, the screen flickers once at the first GUI update.
- Wake-up by USER_SW3 only. The SLEEP button does not wake the system.
- The HDMI output is fixed to 1920x1080@60 (no EDID). There is no
  `/dev/fb0` and no kernel console on HDMI.
- RIIC0 is used by the CM33 and is not available in Linux. Audio is
  disabled: its clock generator is on RIIC0.
- OpenAMP / RPMsg cannot be used on the CM33 at the same time.

---

## 7. Deliverables

```plaintext
rzg3l-lowpower-display/
├── README.md
├── docs/
│   ├── RZG3L-LowPower-Display-Guide.md           # this guide (Markdown)
│   └── RZG3L-LowPower-Display-Guide.pdf          # this guide (PDF)
│
├── layers/
│   ├── meta-rzg3l-seamless-logo-still/           # boot logo, display take-over
│   └── meta-rzg3l-lp-display/                    # the LowPower Display
│
└── image/                                        # prebuilt eSD image
    ├── README.md
    ├── lpdemo-image-smarc-rzg3l.rootfs.wic.gz
    └── lpdemo-image-smarc-rzg3l.rootfs.wic.bmap
```
