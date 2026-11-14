# Game of Life Colour Handheld

This is a handheld Conway's Game of Life with a colour TFT screen. It uses an RP2040. It has colour and trail modes, a rule explorer, and 6 other cellular automata, including "Larger than Life".

![Game of Life Colour Handheld](https://github.com/user-attachments/assets/28ee7e96-d396-44b6-94ed-0935351b37eb)

![Game of Life Colour Handheld screen](https://github.com/user-attachments/assets/92890d19-b4a4-445a-8f03-1f27be04ff47)

## Build guide and video

- Step-by-step build guide on Instructables: [Conway's Game of Life Handheld - COLOURED Version](https://www.instructables.com/Conways-Game-of-Life-Handheld-COLOURED-Ve/)
- Video on YouTube: [Game of Life Colour Handheld](https://youtu.be/rWrENXPgask)

## Features

- Conway's Game of Life with preset, random, symmetric and custom patterns
- 4 cell sizes, from 160 × 120 cells to 40 × 30 cells
- Colour mode and trail mode
- A rule explorer with 9 famous rule sets, and an editor for your own rules
- Other automata: Brian's Brain, Day & Night, Seeds, Cyclic CA and Larger than Life
- Tools: sound, volume, wrap-around or open edges, grid and population counter

The [user guide](docs/MANUAL.md) tells you how to use all the menus and controls.

## What you need

### Hardware

- Raspberry Pi Pico or Waveshare RP2040-Zero
- ST7789 colour TFT display
- All parts are in the [parts list](Parts%20List.pdf).
- The schematic is [Schematic.png](Schematic.png).
- The Gerber files are in [Gerber Files](Gerber%20Files/). You can send them to a PCB maker.

### Software

- Arduino IDE 2
- Board package: **Raspberry Pi Pico/RP2040** by Earle Philhower
- Libraries (install them with the Library Manager): **Adafruit GFX Library** and **Adafruit ST7735 and ST7789 Library**

## Upload the firmware

1. Download this repository. Click **Code → Download ZIP**, then extract the ZIP file.
2. Install the software above.
3. Open `Game_of_Life_Colour_V2/Game_of_Life_Colour_V2.ino`.
4. Connect the RP2040 board to your computer with a USB cable.
5. In the **Tools** menu, set **Board** to your board (**Raspberry Pi Pico** or **Waveshare RP2040 Zero**), and set **Port**.
6. Click **Upload**.

## Files in this repository

| Folder or file | Contents |
|----------------|----------|
| `Game_of_Life_Colour_V2/` | Firmware |
| `Eagle PCB Files/` | Eagle files for the PCB |
| `Front Panel Design Files/` | KiCad, SVG and DXF files for the front panel |
| `Gerber Files/` | Gerber files, ready to order |
| `Parts List.pdf` | Parts list |
| `Schematic.png` | Schematic |
| `docs/MANUAL.md` | User guide |

## Support

Do you like this project? You can support my work on [GitHub Sponsors](https://github.com/sponsors/lonesoulsurfer).

## Licence

- **Code:** [MIT License](LICENSE). You can use, change and sell it.
- **Hardware** (PCB, panels and Gerber files): [CERN Open Hardware Licence v2, Strongly Reciprocal](LICENSE-HARDWARE). You can make, change and sell boards. If you share or sell a changed design, you must also share the changed design files under the same licence.

Copyright (c) 2026 Marcus (lonesoulsurfer).
