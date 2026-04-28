# nrfx SPIM Example — nRF54L15

A minimal Zephyr application that demonstrates using the **nrfx SPIM driver directly** (bypassing the Zephyr SPI driver) on the nRF54L15. The application transfers 255 bytes every 1 second using interrupt-driven, non-blocking transfers and verifies each transfer with a loopback test.

## What it does

1. **Initialises SPIM21** via the nrfx API at 4 MHz, interrupt-driven mode.

2. **Transmits 255 bytes every 1 second** using a `k_timer` to trigger each transfer. The TX buffer is pre-filled with an incrementing pattern `0x00–0xFE`.

3. **Receives 255 bytes** simultaneously (SPIM is full-duplex).

4. **Verifies the loopback**: every received byte is compared against the transmitted byte. Reports `PASS` or `FAIL` over the serial console:
   ```
   #1 PASS: 255 bytes OK
   #2 PASS: 255 bytes OK
   ...
   ```

5. **Reports the first mismatched byte** on failure:
   ```
   #1 FAIL at byte 42: TX=0x2A RX=0xAA
   ```

## Platform

| Item | Value |
|---|---|
| Board | `nrf54l15dk/nrf54l15/cpuapp` |
| SDK | nRF Connect SDK v3.3.0-rc2 |
| Zephyr | v4.3.99 |
| Toolchain | Zephyr SDK 0.17.0 (ARM GCC 12.2.0) |

## nRF54L15 SPIM notes

- SPIM21 is used because it is not occupied by any default board driver. SPIM20 and SPIM22 are also available.
- SPIM21 core clock is **16 MHz**; maximum supported clock is 8 MHz. This example runs at 4 MHz.
- The nrfx driver is used directly. `CONFIG_SPI` is **not** enabled — no Zephyr SPI driver is involved.
- The `IRQ_CONNECT` macro wires the hardware IRQ to `nrfx_spim_irq_handler` at compile time; `irq_enable()` unmasks it at runtime.
- EasyDMA requires that TX and RX buffers reside in **SRAM** (not flash). Static global arrays satisfy this requirement automatically.

## Pin assignment

Configured in [boards/nrf54l15dk_nrf54l15_cpuapp.overlay](boards/nrf54l15dk_nrf54l15_cpuapp.overlay).

| Signal | Pin | Expansion header |
|---|---|---|
| SCK  | P1.11 | Pin 11 |
| MOSI | P1.13 | Pin 13 |
| MISO | P1.12 | Pin 12 |

## Loopback wiring

Connect a jumper wire between **P1.13 (MOSI)** and **P1.12 (MISO)** on the expansion header.  
Without the wire the RX buffer stays at `0xAA` and every transfer reports `FAIL`.

> **Note:** P1.13 is also connected to Button SW0 on the DK. The button's 10 kΩ pull-up has no practical effect at 4 MHz and no button driver is active in this example.

## Building and flashing

```bash
cd nrfx_spim_example
west build -b nrf54l15dk/nrf54l15/cpuapp --pristine
west flash
```

## Serial output

Connect to VCOM1 (e.g. COM90) at **115200 baud**. Expected output after reset with loopback wire fitted:

```
[00:00:00.000] nrfx SPIM example starting on nRF54L15 DK
[00:00:00.000] SPIM21  SCK=P1.11  MOSI=P1.13  MISO=P1.12
[00:00:00.000] Loopback test: connect P1.13 (MOSI) <--> P1.12 (MISO)
[00:00:00.000] SPIM21 initialised at 4 MHz. Sending 255 bytes every 1 s ...
[00:00:01.000] #1 PASS: 255 bytes OK
[00:00:02.001] #2 PASS: 255 bytes OK
[00:00:03.001] #3 PASS: 255 bytes OK
```

Serial settings:
- Baud rate: 115200
- Data bits: 8
- Stop bits: 1
- Parity: None

## Project structure

```
nrfx_spim_example/
├── CMakeLists.txt
├── prj.conf
├── README.md
├── boards/
│   └── nrf54l15dk_nrf54l15_cpuapp.overlay
└── src/
    └── main.c
```

## How it works

1. **IRQ wiring** (`IRQ_CONNECT`): wires the SPIM21 hardware interrupt to `nrfx_spim_irq_handler` at compile time. `irq_enable()` unmasks the line in the NVIC.

2. **Driver init** (`nrfx_spim_init`): configures SPIM21 with `NRFX_SPIM_DEFAULT_CONFIG` (mode 0, MSB first, orc=0xFF, no CS) at 4 MHz and registers `spim_event_handler` as the callback.

3. **Periodic timer** (`k_timer`): fires every 1 second from the system clock, giving `trigger_sem` to unblock the main thread.

4. **Transfer** (`nrfx_spim_xfer`): submits a 255-byte full-duplex `NRFX_SPIM_XFER_TRX` descriptor. The function returns immediately; the SPIM hardware performs the DMA transfer autonomously.

5. **Completion** (`spim_event_handler`): called from ISR context on `NRFX_SPIM_EVENT_DONE`; gives `done_sem` to unblock the main thread.

6. **Verification**: the main thread compares `rx_buf[i]` against `tx_buf[i]` for all 255 bytes and logs the result.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Every transfer reports `FAIL` | Loopback wire not connected (P1.13 ↔ P1.12) |
| `Transfer timed out!` | IRQ not firing — check `IRQ_CONNECT` priority or `irq_enable()` |
| `nrfx_spim_init failed` | SPIM21 already claimed by another driver; check `spi21` DT status |
| No serial output | Wrong COM port; try VCOM1 at 115200 baud |

## License

SPDX-License-Identifier: Apache-2.0
