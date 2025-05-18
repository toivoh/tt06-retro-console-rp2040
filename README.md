Demo for AnemoneGrafx-8 retro console in silicon
================================================
This a demo for my retro console AnemoneGrafx-8, which was made into silicon with the aid of https://tinytapeout.com/.
For more details, see https://github.com/toivoh/tt06-retro-console.
A capture of the demo can be seen at https://youtu.be/j8XpiC0cEMM.

This repository contains the code that runs on the RP2040 microntroller on the TT06 demo board in order to run the demo.
The code has two functions:
- Run a custom RAM emulator that gives the console read access to 128 kB of the RP2040's RAM, acting as VRAM
- Manipulate the contents of VRAM to display the demo's graphics

The RP2040 is clocked at 100.8 MHz, and clocks the console at 50.4 MHz (so that it can produce a VGA pixel every other cycle).
The memory interface uses the console's 4 address pins `addr_out` and 4 data pins `data_in`.
A 16 bit address is clocked out every 4 cycles, and a 16 bit data value is clocked in every 4 cycles, giving a data rate of 25.2 MB/s.
The timing of the first read is signaled by the console bringing the address lines high for the first time. Subsequent reads are made every 4 cycles.
For more about the read only memory interface, see https://github.com/toivoh/tt06-retro-console/blob/main/docs/info.md#read-only-memory-interface.

The console expects the timing of the RAM emulator to be such that data from a read arrives back just in time to influence the address calculation 4 reads later.
The PIO code in this repository has been tweaked to give the right timing on the TT06 demo board. Since the RP2040 runs twice as fast as the TT chip, tuning can be done down to half cycles for the TT chip to improve timing margins. I had to adjust the address delay by a half cycle compared to the earlier version for FPGA, to avoid flickering.

The RAM emulator uses the RP2040's PIO and DMA functions: (No CPU involvement is needed when the RAM emulator is running, which is necessary for predictable timing.)
- A PIO program is used to receive a 16 bit address every 4 cycles (from the `addr_out` pins)
- The address is sent by DMA to the `READ_ADDR_TRIG` register of the read DMA channel
- This triggers the read DMA channel to read 16 bits of data and send them to a second PIO program
- The second PIO program sends the 16 bit data to the `data_in` pins over 4 cycles

The demo code sets up tile graphics and tile maps in VRAM, and manipulates tile maps, sprites lists, and copper lists (for scan line timed register writes) to run the demo.
The copper lists are double buffered: Use one while preparing the other. The copper restarts at address `0xfffe` during every vblank, and the copper lists are switched by writing a PPU jump to `0xfffe`.
For frame synchronization, the demo waits for vsync to start by busy-waiting on the `vsync` pin. When vsync starts, the copper has already been started, and the jump can be changed to the next copper list.

AnemoneGrafx-8 also contains a 4-voice synth, which relies on a second memory interface. I haven't tried to get it to work yet.

The code is also based on
- Mike Bell's Tiny Tapeout 04+ demo board C example https://github.com/MichaelBell/tt-rp2040, and
- the linker script from Mike Bell's RP2040 SPI RAM emulator https://github.com/MichaelBell/spi-ram-emu to get 128 kB of aligned memory on the RP2040
