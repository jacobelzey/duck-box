# Duck Box firmware

Firmware for the pirate "drop a duck / take a duck" door box: a Heltec WiFi
LoRa 32 **V2**, two break-beam sensors, four candle LEDs behind the duck
emblem. No WiFi, no Bluetooth, no network of any kind.

**Status (14 Oct 2026): running in the finished box.** Version 1.1.1 was
flashed on 13 Oct, ran about 68.5 hours on a breadboard off three lithium
AAs, and passed the beam test and a full bench check once soldered into the
box. The "Not verified" list at the end was written before any of that and
is kept as the record of what was assumed.

It saves battery (see "Power"): the beam emitters are lit for
a fraction of a millisecond a hundred times a second, the processor sleeps
in between. The screen is on all the time. **Both emitter
red wires go to GPIO12, never to Vext.** The power saving is behind one
switch, `POWER_SAVE` in `include/config.h`.

> **WARNING: never connect the AA pack to the 5V pin while USB is plugged
> in.** The 5V header pin is wired straight to the USB 5 V line (Heltec
> schematic), so USB would push current back into the cells, which
> non-rechargeable cells (lithium ones above all) must never get.
> Unplug the pack (or switch it off) before plugging in USB, every time.
> **Better: put a diode in the pack's red lead** (see "Wiring", row 1).
> Then USB cannot reach the cells at all.

## Quick start

```sh
pipx install platformio          # or: install the "PlatformIO IDE" extension in VS Code
cd duckbox
pio run                          # build
pio run -t upload                # flash over USB (AA pack disconnected!)
pio device monitor               # serial log, 115200 baud
```

If the upload cannot connect: hold PRG, tap RST, release PRG, run the upload
again. Windows and macOS may need the Silicon Labs CP210x USB driver.

## Wiring

Use the pin labels printed on the board. The header positions are from the
Heltec schematic and are only a cross-check (pin 1 of each header is GND).

| # | From | To (board label) | Header position | Notes |
|---|------|------------------|-----------------|-------|
| 1 | AA pack + (3 cells), through a diode, stripe towards the board | **5V** | left header pin 2 | Not while USB is plugged in. 3 cells only, never 4. See "The battery lead" below. |
| 2 | AA pack - | **GND** | left pin 1 | |
| 3 | Drop beam emitter + | **GPIO12** | right pin 16 | Both emitter + wires on this one pin. **Not Vext** (see below). |
| 4 | Drop beam emitter - | **GND** | | |
| 5 | Drop beam receiver + | **Vext** | left pin 3 or 4 | Vext is 3.3 V, switched on by the firmware |
| 6 | Drop beam receiver - | **GND** | | |
| 7 | Drop beam receiver OUT | **GPIO32** | right pin 10 | Optional: 10 k from OUT to 3V3 if readings are flaky (costs about 0.3 mA each with POWER_SAVE on) |
| 8 | Shelf beam emitter + | **GPIO12** | right pin 16 | Same pin as row 3 |
| 9 | Shelf beam emitter - | **GND** | | |
| 10 | Shelf beam receiver + | **Vext** | left pin 3 or 4 | |
| 11 | Shelf beam receiver - | **GND** | | |
| 12 | Shelf beam receiver OUT | **GPIO33** | right pin 11 | Optional: 10 k from OUT to 3V3 |
| 13 | **GPIO17** -> 150 ohm -> LED1 anode (long leg), and through its own 150 ohm to LED3 anode | LED1 and LED3 cathodes -> **GND** | left pin 17 | 220 ohm also fine |
| 14 | **GPIO22** -> 150 ohm -> LED2 anode, and through its own 150 ohm to LED4 anode | LED2 and LED4 cathodes -> **GND** | left pin 9 | |
| 15 | GPIO23 | not connected | left pin 11 | Was LED3. The firmware still drives it; harmless. |
| 16 | GPIO13 | not connected | right pin 17 | Was LED4. Same. |

The box as built runs the four LEDs as two pairs (rows 13 and 14) to save
two wires; the firmware did not need changing. Each LED keeps its own
resistor.

"Left" is the header with 5V, Vext, RX, TX, RST; "right" is the one with
3V3 and GPIO36-39. Which sensor wire is +, - and OUT is in the sensor
listing; it is not something this firmware can know.

**The battery lead.** Three fresh lithium AA cells read about 5.4 V with
nothing connected (Energizer: 1.79 to 1.83 V per fresh cell) and drop
towards 4.5 V under load. The USB chip on the board is specified for 4.0
to 5.25 V at its supply input (CP2102 data sheet; whether the 5V pin
reaches that input on this board has not been checked). A diode in the
pack's red lead, stripe towards the board, takes the fresh pack back under
that figure and, more important, stops USB from ever pushing current into
the cells. A Schottky diode (1N5817 to 1N5819) loses about 0.3 V, an
ordinary 1N4001 to 1N4007 about 0.7 V; either will do. The price is that
the box stops a little earlier at the very end of the cells' life (lithium
cells hold their voltage until they are nearly empty, so it is little).

**The emitters and GPIO12.** The firmware switches the emitters on and off
through GPIO12 (about 20 mA for the two, which one pin can supply).

- Join the two emitter + wires and solder the joint well. If that wire
  loses contact, both beams go dark together. With both beams clear at
  that moment the box notices, counts nothing and shows CHECK BEAMS. With
  one beam already blocked (full chamber, parked duck) it cannot tell, and
  the other beam counts once. Either way it sees no ducks until the wire
  is fixed.
- GPIO12 is a "strapping" pin: the chip reads it at reset, and it must not
  be HIGH then. An emitter (an LED and a resistor to GND) can only pull it
  down, so this is safe. **Never connect GPIO12 to 3V3, Vext or 5V**: the
  board would not start.
- An emitter wired to Vext by mistake is lit all the time.
  With `POWER_SAVE` on, the box then reads its beams upside down, counts
  nothing, and says `NO LIGHT SEEN` on serial and CHECK BEAMS on screen.

Already on the board (nothing to wire): OLED on GPIO4 (SDA), GPIO15 (SCL),
GPIO16 (reset); Vext switch on GPIO21 (LOW = on); PRG button on GPIO0; white
board LED on GPIO25; LoRa radio on GPIO5/19/27/18/14/26/35/34.

Things the schematic shows that are worth knowing:

- **The OLED is powered from Vext**, the same rail as the sensors. The
  firmware therefore keeps Vext on all the time; the sensors cannot be
  switched off without the screen going dark too.
- **GPIO32 and GPIO33 also carry the board's 32.768 kHz crystal** (not used
  by this firmware). For slow on/off signals like these it should not
  matter, but it has not been tried on hardware. If a beam misbehaves in
  the beam test, fit the 10 k pull-up first.
- **GPIO13 (LED4) also carries the battery-sense divider** (220 k / 100 k)
  on V2 boards (moved to GPIO37 on V2.1). Harmless for driving an LED.
- No battery gauge: the divider measures the LiPo connector, not the AA pack.

## Using it

| PRG button | What happens |
|------------|--------------|
| Short press | Normal -> Dim -> Stealth (screen and LEDs off, still counting) -> Normal. Acts about half a second after the press. If the screen has been set to go dark when idle and is dark at that moment, the first press only lights it. |
| Double press | "RESTOCKING": nothing is counted for 3 minutes. Double press again to end it early. |
| Hold 5 s | Countdown, then all tallies are reset to zero. Let go early to cancel. |
| Hold right after power-on | Beam test mode (below). |

What is counted:

- **Ducks dropped**: one per duck through the drop beam.
- **Plundered**: one per hand reaching into the shelf. It cannot tell how
  many ducks were taken.
- **Trades**: a drop and a reach-in within 60 s of each other, either order.
- **HOLD FULL**: drop beam blocked for 4 s. Nothing more is counted from it
  until it has been clear for 2 s.
- **DUCK ADRIFT**: shelf beam blocked for 15 s (a duck parked in the beam).
  Nothing is counted from it until it has been clear for 2 s.

**The screen** is on all the time, except in Stealth. To save about 5 mA
(an estimate) it can be made to go dark when nothing is happening: set
`SCREEN_PEEK_EVERY_MS = 20000` in `include/config.h`. Then, 30 s after the
last duck, hand or button press it goes dark and shows one page for 4 s
every 20 s; any duck, hand or button press lights it again.
The duck emblem glows all the time (except in Stealth).

The box always starts in Normal mode. Tallies are written to flash 5 s after
the last change, so a duck dropped in the last 5 s before a power cut is
lost. That is deliberate (flash wear).

### "LAST RUN" - measuring battery life

At every start the screen shows `LAST RUN  N h M min` for 3 s: how long the
previous run lasted, to the nearest 10 minutes below. Put in fresh cells,
let them run flat, and read the number at the next power-up.

- A run shorter than 10 minutes is not recorded and leaves the number alone
  (so the restart loop of dying batteries does not wipe it out).
- A bench session longer than 10 minutes does replace it.

## Beam test mode

**How to enter, exactly:**

1. Power the box on (or tap RST) with PRG **not** pressed.
2. As soon as `DUCK BOX / HOLD PRG = TEST` appears, press and hold PRG.
   You have 3 seconds.
3. When the `BEAM TEST` screen appears, let go.

Do not hold PRG while applying power or while tapping RST: GPIO0 is a boot
pin, and the chip then starts its serial bootloader instead (blank screen).
If that happens, let go and power-cycle.

```
BEAM TEST  LED2=IO22        <- LED lit right now and its GPIO
BEAM  STATE IN BLK  #
DROP  clear H  L     3      <- IN = level on the pin now, BLK = level that
SHELF BLOCK L  L     1         means "blocked", # = breaks counted
tap=exit hold=relearn
```

- A blocked beam shows `BLOCK` in inverse, and the white LED on the board
  lights while either beam is blocked (useful when aligning).
- `*` after BLK = polarity fixed in `config.h`. `?` = the level was not
  steady while it was being measured.
- The four candle LEDs light one after another, 1 s each, LED1 to LED4.
- **Tap** PRG to leave. **Hold** PRG about 1 s (both beams clear) to measure
  the polarity again, for example after aligning a sensor.
- Nothing is saved in this mode. Every beam change is also printed on serial.
- Left alone for 15 minutes, the box leaves test mode by itself.

### Bench-test checklist

Do this on USB with the AA pack disconnected, serial monitor open.

1. Power on with both beams clear. The serial log should show
   `cpu: 80 MHz`, `power save: on (... both emitters on GPIO12)`,
   `bluetooth: off`, `lora: SX1276 found, asleep`, `nvs: ok` and two
   `beam ...` lines, each with `100% of samples` and
   `light seen after N us, emitter lead M us`. No `NO LIGHT SEEN`, no
   `WARNING`. **Write the two N values down.** They should be well under
   1400 and close to each other.
2. Enter beam test mode.
3. LEDs: LED1, LED2, LED3, LED4 light in that order, and the screen names
   the GPIO of the one that is lit. A dark LED = wiring or polarity of that
   LED; wrong order = two LED wires swapped.
4. Both rows read `clear`. Block the drop beam with a finger: the DROP row
   shows `BLOCK`, IN flips, # goes up by one. Same for the shelf beam.
   Keep a beam blocked while the LEDs chase: the row must stay `BLOCK`
   (if it flickers, LED light is reaching that receiver).
5. Wave a finger through each beam quickly. # must go up by exactly one
   each time. If it goes up by several, or by itself, fit the 10 k
   pull-ups. (A break shorter than about 16 ms can be missed. A duck
   pushed through the hole should take several times that, but nobody has
   timed one: step 7 is the real test.) Covering both beams in the same
   instant shows nothing for half a second and then BLOCK on both with a
   `WARN` on serial: that is the loose-wire guard, see "Wiring".
6. Tap PRG. After `LAST RUN` the box is in normal operation.
7. Drop a duck: `AHOY!`-type message, LEDs flare, serial prints
   `DROP drops=1 ...`. Drop one that tumbles: still one count. Drop
   twenty: twenty.
8. Reach into the shelf: `REACH_IN`. Within a minute of a drop it also
   prints `TRADE`.
9. Leave something in the drop beam for 4 s: `FULL_ON`, screen says
   HOLD FULL. Remove it: `FULL_OFF` 2 s later.
10. Short press, double press and 5 s hold all behave as in the table
    above. The bar of the reset countdown grows smoothly.
11. Leave the box alone for five minutes and read the `POWER` lines (one a
    minute, see "Power"). Expect roughly `awake=55-60%`, `emit=6%` or so,
    `leds=29%`, `screen=100%`, `sleeps` near 5700, `refused` near 0,
    `slowest` 20 ms or less after the first minute, `lead=` as in step 1,
    and no `NOTE` or `WARN` lines. The screen stays on and changes page
    every few seconds.
12. Wiggle the emitter wire at GPIO12: no `DROP`/`REACH_IN`/`TRADE` may
    appear together out of nothing.
13. Pull the OUT wire of one receiver off its pin (or hold that receiver
    in direct sunlight): `WARN ... sees light while its emitter is dark`
    and CHECK BEAMS within a second or two. Put it back; the warning is
    taken back half a minute later.
14. Power-cycle ten times and tap RST ten times with everything connected:
    the box must start every time (this is the check on GPIO12).
15. Wait for a `SAVED` line, pull the power, power on: the tallies are back.
16. Only now unplug USB. Measure the pack's voltage first (fresh lithium
    cells: about 5.4 V; with the diode in the lead, less at the board).
    Then connect it **with a multimeter in series with the pack's + lead**
    (mA range) and write down the current in Normal, Dim and
    Stealth. See "Power" for what to expect.

### Beam check

At power-up the firmware works out by itself which level (LOW or HIGH)
each receiver shows when it gets no light.

With `POWER_SAVE` on it does that with the emitters dark, so it is right
whatever is in the beams at that moment. It then lights each emitter a few
times and measures how quickly the receiver answers (`light seen after
N us`); from then on the emitter is lit for twice that plus a margin
before the receiver is read (`emitter lead`).

`NO LIGHT SEEN` on a beam line, the `WARNING` under it and CHECK BEAMS on
the screen mean a receiver did not answer its emitter:

- something is in that beam (a full chamber, a duck parked on the shelf):
  harmless, the beam is treated as blocked and is measured as soon as it
  clears;
- emitter and receiver do not face each other, or a wire is off;
- on both beams at once: the emitters are not on GPIO12 (see "Wiring").

Leave `DROP_BEAM_POLARITY` and `SHELF_BEAM_POLARITY` on `AutoDetect`.

With `POWER_SAVE` off the emitters are lit all the time, and the firmware
can only assume that both beams are clear at power-up. **If a beam is
blocked at that moment it reads inverted until the next power-up.** In that
mode, set both constants in `include/config.h` to what the beam test
shows in the BLK column:

```cpp
constexpr BeamPolarity DROP_BEAM_POLARITY  = BeamPolarity::BlockedIsLow;   // BLK = L
constexpr BeamPolarity SHELF_BEAM_POLARITY = BeamPolarity::BlockedIsLow;   // or BlockedIsHigh for H
```

## Configuration (`include/config.h`)

Every pin and number is in this one file, with a comment on each. The ones
you are most likely to touch:

| Constant | Default | What it does |
|----------|---------|--------------|
| `POWER_SAVE` | on | Master switch for everything in "Power". Off = emitters lit all the time, no sleep, screen always on. |
| `LIGHT_SLEEP` | `true` | Processor sleeps between cycles. `false` keeps the pulsed beams and LEDs but stays awake. |
| `SCREEN_STAY_ON_MS` | 30000 | Screen stays on this long after a duck, hand or button. |
| `SCREEN_PEEK_EVERY_MS` / `SCREEN_PEEK_ON_MS` | 0 / 4000 | `EVERY` = 0 (as shipped): the screen never goes dark. `EVERY` = 20000: after the stay-on time it is lit for `ON` ms in every period. `ON` = 0: dark until the next event. |
| `CYCLE_MS` | 10 | Beams are looked at, and the LEDs flashed, once per cycle. |
| `PIN_EMIT_DROP`, `PIN_EMIT_SHELF` | 12, 12 | Emitter supply pin(s). |
| `POWER_REPORT_MS` | 60000 | How often the `POWER` line is printed (0 = never). |
| `DROP_BEAM_POLARITY`, `SHELF_BEAM_POLARITY` | `AutoDetect` | Level that means "blocked". Leave on `AutoDetect` with `POWER_SAVE` on. |
| `DISPLAY_FLIP_180` | `false` | `true` turns the picture upside down. |
| `DROP_DEBOUNCE_MS` | 5 | Drop beam must stay blocked this long to count. |
| `DROP_LOCKOUT_MS` | 400 | After a duck, the beam must stay clear this long before the next break counts (tumbling duck = one). |
| `DROP_FULL_AFTER_MS` / `DROP_FULL_CLEAR_MS` | 4000 / 2000 | Blocked this long = FULL / clear this long = no longer full. |
| `SHELF_MIN_BLOCK_MS` | 60 | Shorter shelf breaks are ignored. |
| `SHELF_LOCKOUT_MS` | 1500 | Breaks this soon after a hand left belong to the same visit. |
| `SHELF_STUCK_MS` / `SHELF_STUCK_CLEAR_MS` | 15000 / 2000 | Parked-duck detection and recovery. |
| `TRADE_WINDOW_MS` | 60000 | Longest gap between a drop and a reach-in that makes a trade. |
| `IDLE_PAGE_MS`, `EVENT_BANNER_MS`, `NOTICE_BANNER_MS` | 4000, 3000, 1800 | How long idle pages, event messages and owner notices stay up. |
| `OLED_CONTRAST_NORMAL` / `OLED_CONTRAST_DIM` | 110 / 1 | Screen brightness, 0-255. |
| `BURNIN_SHIFT_MS` | 60000 | The picture moves 1-2 pixels this often. |
| `LED_AVERAGE_BRIGHTNESS` | 0.30 | Mean LED duty, 0-1. The main battery knob: see "Power". |
| `LED_DIM_FACTOR` | 0.35 | LED brightness in Dim mode, relative to Normal. |
| `LED_FLICKER_DEPTH` | 0.55 | 0 = steady, 0.8 = stormy. |
| `LED_FLARE_PEAK`, `LED_FLARE_DECAY` | 1.0, 0.90 | Flare on an event: peak duty and fade per 20 ms step. |
| `BUTTON_SHORT_MAX_MS`, `BUTTON_DOUBLE_GAP_MS`, `BUTTON_HOLD_RESET_MS` | 700, 400, 5000 | Button timing. |
| `RESTOCK_PAUSE_MS` | 180000 | Length of the restocking pause. |
| `SAVE_QUIET_MS` / `SAVE_MAX_WAIT_MS` | 5000 / 60000 | Tallies are saved this long after the last change / at the latest this long after the first. |
| `RUNTIME_SAVE_MS` | 600000 | How often the running time is saved. |
| `SPLASH_MS` | 3000 | Splash time = window for entering beam test. |
| `CPU_FREQ_MHZ` | 80 | Keep equal to `board_build.f_cpu` in `platformio.ini`. |

The remaining constants (`VEXT_SETTLE_MS`, `BEAM_POLARITY_SAMPLE_MS`,
`BEAM_POLARITY_MIN_AGREE_PCT`, `BEAM_RESYNC_MS`, `OLED_I2C_HZ`,
`UI_REFRESH_MS`, `LAST_RUN_MS`, `TEST_ENTRY_HOLD_MS`, `BEAM_WARNING_MS`,
`LED_PWM_HZ`, `LED_PWM_BITS`, `LED_STEP_MS`, `BUTTON_DEBOUNCE_MS`,
`TEST_LED_STEP_MS`, `TEST_LED_BRIGHTNESS`, `BEAM_TEST_TIMEOUT_MS`,
`LOOP_DELAY_MS`, `SERIAL_BAUD`, and the `EMIT_...` timing constants)
are explained where they are defined and should not need changing.

The words on the screen are all in `src/phrases.h`. Keep lines short and in
capitals; the layout picks the largest type that fits.

## Serial log

115200 baud, one line per event, time since power-on in seconds:

```
[   12.345] DROP drops=5 reach_ins=3 trades=2
[   14.900] REACH_IN drops=5 reach_ins=4 trades=2
[   14.900] TRADE drops=5 reach_ins=4 trades=3
[   20.010] SAVED drops=5 reach_ins=4 trades=3
```

Others: `FULL_ON`, `FULL_OFF`, `SHELF_STUCK_ON`, `SHELF_STUCK_OFF`, `MODE`,
`WAKE`, `RESTOCK_START`, `RESTOCK_END`, `RESET`, `RESET_CANCELLED`,
`RUNTIME`, `LAST_RUN`, `RUNNING`, `BEAM_TEST`, `TEST`, `POWER` (see
"Power"), `NOTE` (something adjusted itself) and `WARN` (something needs
looking at).

With `POWER_SAVE` on, times are those of the look that noticed the change
(up to one cycle after it happened), and a long line can arrive in two or
three bursts because the processor sleeps in between.

## Power

Goal: five days on the door with one change of batteries, on three lithium
AA cells (Energizer Ultimate Lithium or similar). Alkaline cells give
noticeably less.

Always, in both modes:

- WiFi: nothing in the firmware refers to the WiFi driver, so it is not in
  the program at all. Bluetooth: never started; the start-up log confirms
  the controller is idle.
- CPU at 80 MHz instead of 240.
- SX1276 LoRa chip is reset and put into Sleep mode at start-up; the log
  shows whether the chip confirmed it.

With `POWER_SAVE` on (the default), the box works in cycles of 10 ms:

1. **Beams.** Both emitters are lit (GPIO12), and after the "emitter lead"
   time (about 0.6 ms with quick receivers) the receivers are read. Light =
   clear, believed at once. No light on a beam that was clear = keep the
   emitter lit and keep looking for 5 ms more; only if it stays dark all
   that time is the beam blocked. Then the emitters go dark again. Lit
   about 6 % of the time instead of 100 %.
2. **Candle.** Each LED is switched on for its share of the cycle (30 % on
   average, flickering) and off again. This is done by the main loop with a
   plain output pin, not with the PWM unit, because the PWM unit stops when
   the processor sleeps.
3. **Everything else** (counting, button, one strip of the screen if the
   picture changed).
4. **Sleep** for the rest of the cycle (light sleep, woken by a timer).

Also: a new picture is sent
to the panel in strips of 8 rows, one per cycle and only where it changed,
so that the beams are never left unwatched for more than a few
milliseconds.

### The POWER line

Once a minute:

```
[  120.645] POWER awake=59% emit=6% leds=29% screen=100% sleeps=5675 refused=0 slowest=10ms lead=630/630us
```

- `awake`: share of the time the processor was not asleep (approximate:
  1.5 ms is added per sleep for waking up).
- `emit`: share of the time the emitters were lit.
- `leds`: average LED duty. `screen`: share of the time the screen was lit.
- `sleeps` / `refused`: sleeps taken, and sleeps the chip turned down for
  being too short. About 5700 and about 0 are normal.
- `slowest`: the longest loop pass. Around 10 ms is normal; a flash write
  makes an occasional 15-25 ms.
- `lead`: emitter lead time in use for the drop / shelf beam.

Rough current from those shares, in mA. **Every figure in this table is an
estimate, not a measurement** (processor: ESP32 data sheet, 20-31 mA
awake at 80 MHz and 0.8 mA in light sleep; emitters: 10 mA each at 3.3 V
per the Adafruit 2167 listing; the rest are guesses):

| Part | mA |
|---|---|
| Processor | 22 x awake (part of "awake" is waking up, at less than full current) |
| Emitters | 20 x emit |
| LEDs | 30 x leds |
| Screen | 6 x screen |
| Receivers, regulator, USB chip, charger chip | about 5 |

With the figures above: 13.0 + 1.2 + 8.7 + 6 + 5 = **about 34 mA in
Normal**, somewhat over 20 in Dim, about 12 in Stealth (estimates, range
roughly 27-42 for Normal; with `POWER_SAVE` off, 55-70 mA). **Measure it**
(bench-test step 16) and trust the meter, not this table.

What that means, taking a lithium AA as about 3000 mAh usable here
(Energizer's manual gives 3600 mAh at 100 mA down to 0.9 V per cell; this
box stops well before that): 34 mA is about 88 hours. **One set lasts
about three and a half days in Normal, not five.** With the one change of
batteries that is planned, five days has a margin of about two.

If the meter says more, in order of effect:

- `LED_AVERAGE_BRIGHTNESS`: the candle is the largest load, and the
  processor has to stay awake while an LED is lit. 0.30 -> 0.20 saves
  roughly 5 mA. Raising it costs the same the other way: 0.5 is roughly
  +10 mA.
- Dim mode overnight (short press), or Stealth.
- `SCREEN_PEEK_EVERY_MS = 20000`: the screen goes dark when idle and peeks
  for 4 s every 20 s (saves roughly 5 mA). With `SCREEN_PEEK_ON_MS = 0` as
  well it only lights for events.
- If `awake` is far above 60 %, or `sleeps` is near 0, the processor is not
  sleeping: look for `NOTE`/`WARN` lines being printed all the time.

Fallbacks, if something misbehaves on the bench:

- `LIGHT_SLEEP = false`: no sleep (about +10 mA), everything else as above.
- `POWER_SAVE` off (`-DDUCK_POWER_SAVE=0`, or change the default in
  `config.h`): same wiring, no power saving; GPIO12 is then held HIGH
  and the emitters are lit all the time.

Limits that come with `POWER_SAVE`:

- A break of the beam shorter than about 16 ms can be missed (5 ms with
  `POWER_SAVE` off).
  A duck pushed through the hole should take several times that; this has
  not been timed.
- A duck and a hand that arrive within the same hundredth of a second are
  held back until one of the beams is clear again, and then both are
  counted. If both beams stay dark for more than half a second, the box
  takes it for a loose emitter wire and counts neither.
- Nothing shorter than 5 ms is ever counted.
- While a flash write is in progress the beams are not looked at. Normally
  that is a few milliseconds; a few times in five days the flash chip
  erases a page, which can take a few tenths of a second.
- Receivers slower than about 1.4 ms get the longest emitter lead (3 ms),
  which costs roughly 9 mA more (emitters lit longer, processor awake
  longer).
- The candle flashes 100 times a second. That is steady to the eye; a
  phone camera may show bands.

Battery notes, from the schematic rather than from measurement: the pack
feeds the 3.3 V regulator through a Schottky diode, so the box will stop
some way before the cells are completely empty, and 3 NiMH cells (3.6 V)
are probably too low to start with. Lithium AA cells hold their voltage
until they are nearly empty, which suits this; alkaline cells sag early.

## Tests

```sh
sh native_tests/run.sh       # unit tests (g++ only)
sh native_tests/run_sim.sh   # whole firmware on a pretend board (needs "pio run" once; Linux/macOS/WSL)
```

`run.sh`: 86 tests of the counting logic, button, candle, save timer, the
beam sampler and the idle-screen rule.

`run_sim.sh` compiles the real `src/*.cpp` and the real U8g2 against small
stand-in headers (`native_tests/sim/stubs`), plays scripted ducks, hands and
button presses, and checks the serial log, LED and emitter outputs, sleep
time, display contents and flash contents. It runs all 44 scenarios three
times: as shipped, with the dark idle screen switched on, and with
`POWER_SAVE` off. The pretend board models the things the
power saving depends on: reading the clock costs time, receivers take time
to answer and to let go, short sleeps are refused and each sleep costs a
wake-up, a flash write stalls the loop. It also writes a picture of every
screen to `.native_build/screens/`; `docs/screens_*.png` are contact sheets
of those pictures. It is a simulation: it shows the logic is wired together
correctly, not that the electronics behave.

## Project layout

```
platformio.ini            board, pinned platform and library versions
include/config.h          every pin and every tunable number
include/logic_config.h    copies config.h values into the logic classes
src/main.cpp              the application: stages, screen choice, button, saving
src/duck_logic.*          counting state machine (plain C++, unit tested)
src/button_decoder.*      press / double press / hold (plain C++, unit tested)
src/candle_flicker.*      flame simulation (plain C++, unit tested)
src/save_timer.h          when to write to flash (plain C++, unit tested)
src/beams.*               the two beams as "blocked / clear", either way of working
src/beam_sampler.*        POWER_SAVE on: judging a pulsed beam (plain C++, unit tested)
src/pulse.*               POWER_SAVE on: emitter and LED pins switched once per cycle
src/power.*               POWER_SAVE on: cycle pacing, light sleep, POWER report
src/screen_policy.h       POWER_SAVE on: when the idle screen is lit (plain C++, unit tested)
src/edge_capture.*        POWER_SAVE off: GPIO interrupt -> timestamped ring buffer
src/ui.*, src/phrases.h   screen layouts, all on-screen text
src/leds.*, src/storage.*, src/radios.*   LED outputs, NVS, LoRa sleep / Bluetooth check
native_tests/             unit tests and the firmware simulation
docs/                     rendered screen pictures
```

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Blank screen after power-on | PRG was held during power-on (bootloader). Power-cycle without touching PRG. |
| Screen stays blank, serial log looks normal | Vext/OLED problem: check that nothing shorts Vext to GND. |
| `HOLD FULL` and `DUCK ADRIFT` with nothing in the box, `NO LIGHT SEEN` on both beams | The emitters get no power: not on GPIO12, or that wire is off. |
| `HOLD FULL` with an empty chamber, `NO LIGHT SEEN` on one beam | That emitter and receiver do not see each other: alignment, a wire, or something in the beam. |
| `CHECK BEAMS` at start | A receiver did not answer its emitter. See "Beam check". Also shown, harmlessly, when the box is switched on with the chamber full. |
| `WARN both beams went dark at the same moment` | The shared emitter wire (GPIO12) lost contact. Nothing was counted. Re-solder it. |
| `WARN ... sees light while its emitter is dark` | Bright light on that receiver, or its OUT wire is loose. That beam cannot see ducks meanwhile. |
| `NOTE ... receiver is slower than assumed` now and then | The firmware gave that beam more time. Harmless; costs a little battery. |
| Board does not start at all with the emitters connected | GPIO12 is being pulled HIGH. It must go to the emitter + wires only. |
| Batteries last much less than "Power" says | Read the `POWER` lines: `awake` far above 60 % or `sleeps` near 0 means it is not sleeping. |
| (POWER_SAVE off) `HOLD FULL` with an empty chamber, or counts on removal instead of on drop | A beam was blocked at power-up (polarity inverted). Power-cycle with beams clear, then fix the polarity in `config.h`. |
| A beam never changes in the beam test | No power to that sensor (Vext, GND), OUT on the wrong pin, emitter and receiver not facing each other. |
| # jumps by more than one per break, or counts by itself | Add the 10 k pull-up from OUT to 3V3; keep sensor wires away from the LED wires. |
| Box behaves as if PRG is pressed while a serial program is open | That program is pulling GPIO0 through the DTR/RTS lines. `pio device monitor` is set up not to. |
| Orange LED on the board blinks | The LiPo charger chip with no LiPo attached. Not controlled by this firmware. (Reported behaviour, not checked here.) |

## Verified, and how

| Fact | Source |
|------|--------|
| PlatformIO board id `heltec_wifi_lora_32_V2` | <https://docs.platformio.org/en/latest/boards/espressif32/heltec_wifi_lora_32_V2.html>, and `boards/heltec_wifi_lora_32_V2.json` in platform-espressif32 6.13.0 (the build uses it) |
| OLED 4/15/16, Vext 21, LED 25, PRG 0, LoRa 5/19/27/18/14/26/35/34 | `variants/heltec_wifi_lora_32_V2/pins_arduino.h` in the Arduino-ESP32 2.0.17 package the build uses (<https://github.com/espressif/arduino-esp32/blob/2.0.17/variants/heltec_wifi_lora_32_V2/pins_arduino.h>), and <https://www.espboards.dev/esp32/heltec-wifi-lora-32-v2/> |
| OLED supply (VDD, VBAT) comes from Vext; Vext is a P-FET from 3.3 V with its gate on GPIO21 (LOW = on, off by default) | Heltec schematics <https://resource.heltec.cn/download/WiFi_LoRa_32/V2/WIFI_LoRa_32_V2(868-915).PDF> and <https://resource.heltec.cn/download/WiFi_LoRa_32/V2.1/WIFI_LoRa_32_V2.1(868-915).PDF> |
| 5V header pin = USB 5 V line (after the fuse); 3.3 V regulator fed through a diode | same schematics |
| GPIO13 battery divider on V2, GPIO37 on V2.1; 8 MB flash on V2 | same schematics, and <https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32/hardware-update-log> |
| 32.768 kHz crystal on GPIO32/33 | same schematics; <https://doc.riot-os.org/boards/esp32-heltec-lora32-v2> lists the crystal as fitted |
| GPIO17, 22, 23, 32, 33, 13 are on the headers | same schematics, espboards.dev table |
| PRG = GPIO0 with 10 k pull-up; board LED on GPIO25, active high | same schematics |
| SX1276: RegOpMode 0x01 (mode bits 000 = Sleep), RegVersion 0x42 reads 0x12, write = address bit 7 | <https://github.com/sandeepmistry/arduino-LoRa/blob/master/src/LoRa.cpp> (cross-check; the Semtech datasheet itself was not fetched) |
| `board_build.f_cpu` is applied at start-up; `attachInterrupt()` handlers are not flash-safe in this core build (why `edge_capture.cpp` uses the ESP-IDF driver) | `cores/esp32/esp32-hal-misc.c`, `esp32-hal-gpio.c`, `esp32-hal.h` and `tools/sdk/esp32/sdkconfig` of Arduino-ESP32 2.0.17 (<https://github.com/espressif/arduino-esp32/tree/2.0.17>) |
| The beam interrupt handler and its data are in RAM | checked in the linked `firmware.elf` (handler at an IRAM address, calls only `esp_timer_get_time`, also in IRAM) |
| U8g2 2.36.18 exists and is the current registry version | PlatformIO registry (`pio pkg search`) |
| GPIO12 and GPIO19 are on the headers (right pin 16, left pin 10); nothing else is on the GPIO12 net | Heltec V2 schematic (above) and <https://www.espboards.dev/esp32/heltec-wifi-lora-32-v2/> |
| A pin may source 40 mA; GPIO12 is a strapping pin read with the pads in their reset state | ESP32 data sheet, <https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf> |
| Light sleep in this core (ESP-IDF 4.4.7, no power management configured): output pins keep their level, the low-power domain with the input pull-ups stays on, serial output is paused and resumed, the clock stays right, requests under about 2.3 ms are refused, each sleep costs about 1.5 ms of waking | Reading `sleep_modes.c`, `sleep_gpio.c` and `uart.c` of ESP-IDF v4.4.7 (<https://github.com/espressif/esp-idf/tree/v4.4.7>) and the linked `firmware.elf`, by an independent reviewer. Not observed on a chip. |
| Break-beam emitter 10 mA at 3.3 V, receiver response under 2 ms, open-collector output | <https://www.adafruit.com/product/2167> (a comparable part, not necessarily the one fitted) |
| Fresh lithium AA: 1.79 to 1.83 V open circuit, 3600 mAh at 100 mA to 0.9 V | Energizer L91/L92 application manual (copy at <https://files.batteryjunction.com/frontend/files/Energizer-L91-L92-Manual.pdf>) |
| CP2102 regulator input 4.0 to 5.25 V, 5.8 V absolute maximum on I/O pins | CP2102 data sheet (copy at <https://www.komponenten.es.aau.dk/fileadmin/komponenten/Data_Sheet/SMD_Data/Integrerede_Kredse/cp2102_1_.pdf>) |

## Not verified

- *(Since resolved: it now runs on the real board, see Status at the top.)*
  **Nothing has run on real hardware.** No board, sensor, LED or OLED was
  available. The build is real; the behaviour is from tests and simulation.
- That your board really is a V2. A V1 uses GPIO32/33 for the radio, and a
  V3 is a different chip; this firmware suits neither. The project is set
  to the 4 MB flash layout so it also starts on a V2-labelled board with a
  smaller flash chip; uploading with these settings was not tested.
- The break-beam sensors: output polarity, behaviour at 3.3 V, whether the
  internal pull-ups are enough, real response time, current draw.
- **Everything in "Power" that involves the chip itself**: that it really
  sleeps a hundred times a second, what that saves, that the pins stay
  put, that the screen and the serial port are happy with it, how the
  100 Hz candle looks. The code follows the ESP-IDF source and passes a
  simulation that was made deliberately unkind, but none of it has been
  seen on a board. The bench test is where this is found out, and
  `LIGHT_SLEEP` / `POWER_SAVE` are the switches if it goes wrong.
- That two emitters on GPIO12 get enough voltage for the 14 cm shelf beam
  (the pin sags a little at 20 mA).
- Whether candle light can reach a receiver. The beams are read with the
  candle dark, except for the instant at the very top of the flare after
  an event, when the LEDs are fully on.
- Whether the 5V pin reaches the USB chip's supply input, and with it
  whether a fresh lithium pack without a diode is inside that chip's
  limits. See "The battery lead".
- That the crystal on GPIO32/33 does not disturb the beam inputs (expected
  to be harmless; if it is not, GPIO36 and GPIO39 are free inputs, but they
  have no internal pull-up, so the 10 k resistors become mandatory, and the
  wiring diagram changes).
- How long a duck really blocks the drop beam (the 5 ms debounce and 400 ms
  lockout are based on an estimate of tens of milliseconds).
- *(Measured since: about 68.5 hours on one set, cells at about 1.49 V when
  pulled.)* Battery life. The estimate in "Power" (about three and a half days per set of
  lithium cells) is arithmetic on typical part figures, not a measurement.
  Measure the current at the bench, and use LAST RUN to find out before
  the trip if there is time.
- Behaviour as the cells run down (brown-out restarts), and whether 3 cells
  give enough headroom through the diode and regulator for a full discharge.
- OLED at 400 kHz I2C on this board (common practice with this board, not
  tried here; `OLED_I2C_HZ` lowers it), and the look of contrast 110 and 1.
- LED brightness and the candle effect behind the printed emblem (the
  numbers are tuned by simulation, not by eye).
- Flash-write timing. With POWER_SAVE off, the claim that a duck is never
  missed during a save rests on the handler being flash-safe, which was
  checked in the binary but not observed on a chip. With POWER_SAVE on the
  beams are simply not looked at during a write (see "Power", limits).
