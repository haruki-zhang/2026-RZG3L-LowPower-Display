# SPDX-License-Identifier: BSD-3-Clause
"""
RZ/G3L LowPower Display Demo: access to the CM33 control block (SRAM 0x5B000).

Python mirror of lpdemo_shared.h (lpdemo-cm33-fw). Keep both in sync.

The block is mapped through UIO (device name "lpdemo-ctrl", uio_pdrv_genirq) as device
memory, so it is only accessed with aligned 32-bit loads/stores (memoryview format "I").
"""

import mmap
import os
import time

CTRL_MAGIC = 0x4344504C          # "LPDC"
CTRL_VERSION = 4
I2C_GRANT = 0x47433249           # "I2CG"
UIO_NAME = "lpdemo-ctrl"

# struct lpdemo_ctrl: one name per 32-bit word, in order
FIELDS = (
    "magic", "version", "size", "fw_version",
    "cm33_state", "heartbeat", "cm33_clk_hz", "uptime_ms",
    "i2c_grant", "period_ms", "baseline_mw", "susp_wake_ms",
    "pm_seq", "pm_status", "pm_count", "pm_err_count",
    "pm_syc_lo", "pm_syc_hi", "vbus_mv", "vshunt_uv",
    "current_ma", "power_mw", "power_avg_mw", "raw_vbus",
    "raw_vshunt", "isl_config", "rshunt_uohm", "i2c_addr",
    "fault_pc", "fault_cfsr", "fault_lr", "reserved0",
    # 0x80: display hand-over, CA55 -> CM33
    "disp_cmd", "disp_req_seq", "auto_return_ms", "sw3_wake",
    "img_w", "img_h", "bg_rgb", "lp_flags",
    # 0xA0: display hand-over, CM33 -> CA55
    "disp_ack_seq", "disp_result", "disp_state", "disp_wake",
    "linux_hdr", "saved_irq_enb", "out_size", "enter_count",
    "leave_count", "disp_err_count", "und_count", "enter_us",
    "leave_us", "sw3_pin", "reserved1", "reserved2",
    # 0xE0: system suspend with the display kept running (DISP_RET, Step 3)
    "susp_mode", "susp_state", "susp_count", "wake_count",
    "susp_result", "pll4_us", "ddr_exit_us", "bl31_stage",
    # 0x100: LP picture placement (Step 4)
    "img_x", "img_y", "bar_w", "bar_h",
    "bar_x", "bar_y", "readout_ms",
    # 0x11C: power read-out (CM33)
    "readout_count", "readout_us", "readout_wait_us", "readout_late",
    "readout_mw", "readout_pct",
    # 0x134: LP power samples (CM33)
    "ring_count", "ring_period_ms", "ring_syc0_lo", "ring_syc0_hi",
    # 0x144: LP display list as the VSPD runs it (CM33)
    "lp_srcrpf", "lp_brs_ctrl1", "readout_px",
    # 0x150
    "readout_sync", "reserved4", "reserved5", "reserved6",
)
INDEX = {name: i for i, name in enumerate(FIELDS)}
SIGNED = {"vbus_mv", "vshunt_uv", "current_ma", "power_mw", "power_avg_mw"}

# Fields published under pm_seq
PM_FIELDS = (
    "pm_status", "pm_count", "pm_err_count", "pm_syc_lo", "pm_syc_hi",
    "vbus_mv", "vshunt_uv", "current_ma", "power_mw", "power_avg_mw",
    "raw_vbus", "raw_vshunt",
)

CM33_STATE = {0: "BOOT", 1: "WAIT_GRANT", 2: "RUN", 0xEE: "FAULT"}
DISP_LINUX = 0
DISP_LP = 1
DISP_STATE = {DISP_LINUX: "LINUX", DISP_LP: "LP"}
DISP_RESULT = {0: "OK", 1: "BUSY", 2: "TIMEOUT", 3: "STATE", 4: "PARAM", 5: "NOT_RUNNING"}
WAKE = {0: "-", 1: "HOST", 2: "USER_SW3", 3: "AUTO"}

# LP layers in SRAM, UIO map 1 = 0x5E000 (lpdemo_shared.h LPDEMO_SRAM_IMG_* / _BAR_*)
RPF0_OFFSET = 0x00000            # 0x5E000
RPF0_SIZE = 0x1F000
RPF1_OFFSET = 0x1F000            # 0x7D000
RPF1_SIZE = 0x1F000
LP_F_READOUT = 1 << 0            # the CM33 redraws the power read-out in RPF1
LP_F_PROGRESS = 1 << 1           # suspend progress bar in RPF1 (debug)
READOUT_NONE = 0xFFFFFFFF

# Frame timing probe (Step 4): struct lpdemo_probe at control block + 0x400
PROBE_OFFSET = 0x400
PROBE_MAGIC = 0x32425250        # "PRB2"
PROBE_FIELDS = ("magic", "count", "duration_us", "samples", "sta_before", "sta_w0c",
                "sta_w0c_100us", "sta_w1c_100us", "sync", "sync_reg", "sync_bit",
                "sync_level", "sync_period_us", "sync_short_us", "nbits", "reserved")
PROBE_REG_FIELDS = ("addr", "first", "toggled", "changes")
PROBE_BIT_FIELDS = ("reg_bit", "changes", "bursts", "gaps", "kind", "period_us", "short_us",
                    "level")
PROBE_BITS = 12
PROBE_REG_NAMES = ("VSPD VI6_STATUS", "VSPD VI6_WPF0_IRQ_STA", "VSPD VI6_DISP_IRQ_STA",
                   "DU_MSR0", "DU_MSR1", "DSI LINKSR", "DSI VICH1SR")
SYNC = {0: "none", 1: "LEVEL", 2: "BURST", 3: "GAP", 4: "LATCH"}

# LP power samples (Step 4): uint16 mW ring at control block + 0x800
RING_OFFSET = 0x800
RING_LEN = 1024
RING_NO_SAMPLE = 0xFFFF
SYC_HZ = 24000000

# DISP_RET (Step 3)
SUSP_MODE_DDR_RET = 1 << 0
SUSP_MODE_PLAIN_WFI = 1 << 1
SUSP_MODE_LATE_RETURN = 1 << 2
SUSP_MODE_DEBUG_UART = 1 << 3
SUSP_STATE = {0: "NONE", 1: "SLEEP", 2: "RETENTION", 3: "WAKING", 4: "RESUMED", 0xEE: "FAILED"}
SUSP_RESULT = {0: "OK", 1: "BAD_RETCSR", 2: "PLL4_TIMEOUT", 3: "DISPLAY"}
BL31_STAGE = {
    0: "-",
    0x3101: "SUSPEND", 0x31A1: "NO_CM33 (suspend skipped)", 0x3102: "SRAM",
    0x3103: "DDR_RETENTION", 0x3104: "PLL4_OFF", 0x3105: "WFI", 0x3106: "WOKEN",
    0x3107: "WARM_ENTRY", 0x3108: "RESUMED",
}
WAKE_STAGE = {0: "-", 0x3301: "TRIGGER", 0x3302: "PLL4_LOCKED", 0x3303: "DDR_EXIT_DONE",
              0x3304: "DISPLAY_RETURNED", 0x3305: "MHU_RAISED"}

# UIO map 2 (SRAM 0x5A000): retcsr snapshot at +0, trace of the last suspend at +0x800
TRACE_OFFSET = 0x800
TRACE_MAGIC = 0x5254504C        # "LPTR"
TRACE_FIELDS = ("magic", "boot_count", "bl31_stage", "cm33_stage", "susp_count",
                "susp_result", "susp_mode", "uptime_ms")
RETCSR_MAGIC = 0x52534352       # "RCSR"
PM_STATUS = {
    0: "OK",
    1: "NOT_GRANTED",
    2: "BUS_BUSY",
    3: "NACK",
    4: "TIMEOUT",
    5: "CLK_FAIL",
    6: "BAD_CONFIG",
}


class LpdemoError(Exception):
    pass


def find_uio(name=UIO_NAME):
    """
    Returns (/dev/uioN, sysfs dir) of the UIO device called name. Without the DT property
    linux,uio-name, uio_pdrv_genirq names the device after the DT node including the unit
    address ("lpdemo-ctrl@5b000"), which is accepted as well.
    """
    base = "/sys/class/uio"
    try:
        entries = sorted(os.listdir(base))
    except FileNotFoundError:
        entries = []
    found = []
    for entry in entries:
        try:
            with open(os.path.join(base, entry, "name")) as f:
                uio_name = f.read().strip()
        except OSError:
            continue
        if uio_name == name or uio_name.split("@", 1)[0] == name:
            return "/dev/" + entry, os.path.join(base, entry)
        found.append("%s=%s" % (entry, uio_name))
    raise LpdemoError(
        "UIO device '%s' not found (UIO devices: %s). Is uio_pdrv_genirq loaded with "
        "of_id=renesas,rzg3l-lpdemo-ctrl?" % (name, ", ".join(found) or "none"))


class Ctrl:
    """The CM33 control block."""

    def __init__(self, name=UIO_NAME):
        self._fd = self._mm = self._w = None
        dev, sysfs = find_uio(name)
        with open(os.path.join(sysfs, "maps", "map0", "size")) as f:
            size = int(f.read(), 0)
        self._fd = os.open(dev, os.O_RDWR | os.O_SYNC)
        # UIO: map N is selected by offset N * page size; the control block is map 0
        self._mm = mmap.mmap(self._fd, size, mmap.MAP_SHARED,
                             mmap.PROT_READ | mmap.PROT_WRITE, offset=0)
        self._w = memoryview(self._mm).cast("I")
        self._sysfs = sysfs
        self._img_mm = None
        self._diag_mm = None
        if len(self._w) < (RING_OFFSET + RING_LEN * 2) // 4:
            self.close()
            raise LpdemoError("UIO map 0 is too small (%d bytes)" % size)

    def close(self):
        for name in ("_img_mm", "_diag_mm"):
            mm = getattr(self, name, None)
            if mm is not None:
                mm.close()
                setattr(self, name, None)
        if self._w is not None:
            self._w.release()
            self._w = None
        if self._mm is not None:
            self._mm.close()
            self._mm = None
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def read(self, name):
        v = self._w[INDEX[name]]
        if name in SIGNED and v & 0x80000000:
            v -= 1 << 32
        return v

    def write(self, name, value):
        self._w[INDEX[name]] = value & 0xFFFFFFFF

    def valid(self):
        return (self.read("magic") == CTRL_MAGIC and
                self.read("version") == CTRL_VERSION)

    def snapshot(self, retries=100):
        """Reads all fields; the pm_* block is read consistently (pm_seq)."""
        d = {name: self.read(name) for name in FIELDS if name not in PM_FIELDS}
        for _ in range(retries):
            s1 = self.read("pm_seq")
            if s1 & 1:
                time.sleep(0.001)
                continue
            pm = {name: self.read(name) for name in PM_FIELDS}
            if self.read("pm_seq") == s1:
                d.update(pm)
                d["pm_seq"] = s1
                d["pm_syc"] = (pm["pm_syc_hi"] << 32) | pm["pm_syc_lo"]
                d["cm33_state_name"] = CM33_STATE.get(d["cm33_state"], "?")
                d["disp_state_name"] = DISP_STATE.get(d["disp_state"], "?")
                d["disp_result_name"] = DISP_RESULT.get(d["disp_result"], "?")
                d["disp_wake_name"] = WAKE.get(d["disp_wake"], "?")
                d["susp_state_name"] = SUSP_STATE.get(d["susp_state"], "?")
                d["susp_result_name"] = SUSP_RESULT.get(d["susp_result"], "?")
                d["bl31_stage_name"] = BL31_STAGE.get(d["bl31_stage"], "0x%x" % d["bl31_stage"])
                d["pm_status_name"] = PM_STATUS.get(pm["pm_status"], "?")
                d["valid"] = d["magic"] == CTRL_MAGIC and d["version"] == CTRL_VERSION
                return d
        raise LpdemoError("pm_seq did not settle (CM33 stuck in an update?)")

    def _diag(self):
        """UIO map 2: SRAM 0x5A000 (retcsr snapshot + trace), 32-bit words."""
        if self._diag_mm is None:
            path = os.path.join(self._sysfs, "maps", "map2", "size")
            if not os.path.exists(path):
                raise LpdemoError("UIO map 2 (SRAM 0x5A000) not available (old DT?)")
            with open(path) as f:
                size = int(f.read(), 0)
            self._diag_mm = mmap.mmap(self._fd, size, mmap.MAP_SHARED, mmap.PROT_READ,
                                      offset=2 * mmap.PAGESIZE)
        return memoryview(self._diag_mm).cast("I")

    def trace(self):
        """Trace of the last suspend (kept across a reset, best effort), or None."""
        w = self._diag()
        try:
            base = TRACE_OFFSET // 4
            t = {name: w[base + i] for i, name in enumerate(TRACE_FIELDS)}
            t["retcsr_valid"] = w[0] == RETCSR_MAGIC
        finally:
            w.release()
        if t["magic"] != TRACE_MAGIC:
            return None
        t["bl31_stage_name"] = BL31_STAGE.get(t["bl31_stage"], "0x%x" % t["bl31_stage"])
        t["cm33_stage_name"] = WAKE_STAGE.get(t["cm33_stage"], "0x%x" % t["cm33_stage"])
        t["susp_result_name"] = SUSP_RESULT.get(t["susp_result"], "?")
        return t

    def grant_i2c(self, grant=True):
        self.write("i2c_grant", I2C_GRANT if grant else 0)

    def set_period(self, ms):
        self.write("period_ms", ms)

    def _write_layer(self, offset, limit, data):
        if self._img_mm is None:
            with open(os.path.join(self._sysfs, "maps", "map1", "size")) as f:
                size = int(f.read(), 0)
            if size < RPF1_OFFSET + RPF1_SIZE:
                raise LpdemoError("UIO map 1 is too small (%d bytes): DT of Step 3 or older?"
                                  % size)
            self._img_mm = mmap.mmap(self._fd, size, mmap.MAP_SHARED,
                                     mmap.PROT_READ | mmap.PROT_WRITE,
                                     offset=mmap.PAGESIZE)
        if len(data) > limit or len(data) % 4:
            raise LpdemoError("LP layer of %d bytes does not fit (%d)" % (len(data), limit))
        # Device memory (UIO maps it non-cached): aligned 32-bit stores only. A slice copy
        # would use memcpy(), which may do unaligned accesses and fault (SIGBUS).
        dst = memoryview(self._img_mm).cast("I")
        src = memoryview(bytes(data)).cast("I")
        try:
            base = offset // 4
            for i in range(len(src)):
                dst[base + i] = src[i]
        finally:
            src.release()
            dst.release()

    def write_lp_layers(self, rpf0, rpf1, bg_rgb, flags=LP_F_READOUT, readout_ms=0):
        """
        Copies the two LP layers (lpdemo_lpimage: (data, w, h, x, y), RGB565 little endian)
        to SRAM (UIO map 1) and sets the parameters the CM33 reads at the next hand-over.
        rpf1 = None: no RPF1 (and no read-out).
        """
        for name, layer, limit in (("RPF0", rpf0, RPF0_SIZE), ("RPF1", rpf1, RPF1_SIZE)):
            if layer is None:
                continue
            data, w, h, x, y = layer
            if len(data) != w * h * 2 or w % 2 or len(data) > limit:
                raise LpdemoError("bad %s layer %dx%d (%d bytes)" % (name, w, h, len(data)))
        if rpf0 is None:
            raise LpdemoError("no RPF0 layer")
        self._write_layer(RPF0_OFFSET, RPF0_SIZE, rpf0[0])
        _, w, h, x, y = rpf0
        for k, v in (("img_w", w), ("img_h", h), ("img_x", x), ("img_y", y)):
            self.write(k, v)
        if rpf1 is None:
            self.write("bar_w", 0)
            flags &= ~LP_F_READOUT
        else:
            self._write_layer(RPF1_OFFSET, RPF1_SIZE, rpf1[0])
            _, w, h, x, y = rpf1
            for k, v in (("bar_w", w), ("bar_h", h), ("bar_x", x), ("bar_y", y)):
                self.write(k, v)
        self.write("bg_rgb", bg_rgb)
        self.write("lp_flags", flags)
        self.write("readout_ms", readout_ms)

    def probe(self):
        """Result of the last frame timing probe of the CM33, or None."""
        base = PROBE_OFFSET // 4
        p = {name: self._w[base + i] for i, name in enumerate(PROBE_FIELDS)}
        if p["magic"] != PROBE_MAGIC:
            return None
        base += len(PROBE_FIELDS)
        p["regs"] = []
        for r, rname in enumerate(PROBE_REG_NAMES):
            reg = {name: self._w[base + 4 * r + i] for i, name in enumerate(PROBE_REG_FIELDS)}
            reg["name"] = rname
            p["regs"].append(reg)
        base += 4 * len(PROBE_REG_NAMES)
        p["bits"] = []
        for k in range(min(p["nbits"], PROBE_BITS)):
            b = {name: self._w[base + 8 * k + i] for i, name in enumerate(PROBE_BIT_FIELDS)}
            b["reg"], b["bit"] = b["reg_bit"] >> 8, b["reg_bit"] & 0xFF
            p["bits"].append(b)
        return p

    def lp_samples(self):
        """
        Power samples of the last LP period: (syc0, period_ms, [mW or None, ...]) of the
        last RING_LEN samples, syc0 = SYC (24 MHz, = CNTVCT) of the first one returned.
        """
        count = self.read("ring_count")
        period = self.read("ring_period_ms") or 2000
        syc0 = (self.read("ring_syc0_hi") << 32) | self.read("ring_syc0_lo")
        first = max(0, count - RING_LEN)
        base = RING_OFFSET // 4
        out = []
        for k in range(first, count):
            i = k % RING_LEN
            word = self._w[base + i // 2]
            v = (word >> 16) if i & 1 else (word & 0xFFFF)
            out.append(None if v == RING_NO_SAMPLE else v)
        return syc0 + first * period * (SYC_HZ // 1000), period, out


def fw_version_str(v):
    return "%d.%d.%d" % (v >> 24, (v >> 16) & 0xFF, v & 0xFFFF)
