# Decisions and measurements

Record every decision, resolved [VERIFY] item and measured number here, with the date and the phase it belongs to.

## Pinned dependency versions

Nothing is pinned yet. Fill in when each dependency is added (adding one needs explicit approval, see CLAUDE.md).

| Dependency | Version / commit | Date pinned | License verified |
|---|---|---|---|
| pico-sdk | _not added_ | | |
| picosystem | _not added_ | | |
| peanut_gb | _not added_ | | |
| minigb_apu | _not added_ | | |

## Open decisions (PLAN.md section 12)

| # | Question | Status |
|---|---|---|
| 1 | Default control mapping and menu combo (plan default: X→Select, Y→Start, hold X+Y ≈1 s for menu) | Open |
| 2 | Host-side UF2 packing acceptable for v1? | Open |
| 3 | Audio required for v1, or off by default? | Open |
| 4 | Repo license | Open |

## Unresolved [VERIFY] items (PLAN.md section 2)

1. SDK framebuffer RAM in 240×240 mode
2. How `flip()` pushes pixels (DMA/PIO, core 1?)
3. Minimum Pico SDK version for `flash_safe_execute()`; PicoSystem SDK compatibility
4. Size of `struct gb_s` in the pinned Peanut-GB
5. `lcd_draw_line` pixel format and gamepad bit layout
6. Stable overclock speed and battery impact
7. `stdio_usb` logging alongside the SDK

## Log

_No entries yet._
