# Interactive Ultrasonic Theremin

> My senior design project (ELEC 491, Koç University, advisor Dr. Levent Beker): a musical
> instrument you play by moving your hands in the air, with no touching. An invention
> disclosure was filed for it.

![The working prototype](docs/images/prototype.jpg)

## What it does

A classic theremin senses your hands through capacitance, which makes it sensitive to weather,
nearby objects and electrical noise, and hard to calibrate. This version keeps the same
gestures but measures hand position with **ultrasound** instead:

- One ultrasonic sensor measures how far one hand is and sets the **pitch**
  (two octaves, C4 to C6).
- The other measures the second hand and sets the **volume**.
- A small OLED screen shows the note, its frequency and the volume while you play.

![Display while playing](docs/images/oled_display.png)

## How it works

![System block diagram](docs/images/block_diagram.png)

- **ESP32 running FreeRTOS** with three tasks: one reads the sensors, one generates audio,
  one draws the display.
- **Smoothing.** Moving-average and exponential filters remove jitter from the distance
  readings so the pitch glides instead of jumping.
- **Sound.** A direct digital synthesis (DDS) oscillator with an ADSR envelope generates the
  tone, output through the ESP32's sigma-delta modulator to an amplifier and speaker.

![Firmware flow](docs/images/firmware_flow.png)

## Hardware

| Schematic | PCB (3D view) |
|---|---|
| ![Schematic](docs/images/schematic.png) | ![PCB render](docs/images/pcb_render.png) |

## Poster

![Project poster](docs/images/poster.jpg)

## Repository structure

```
ultrasonic-theremin/
├── firmware/main/ultrasonic_theremin.c   # ESP-IDF application (FreeRTOS tasks, DDS, filters)
└── docs/
    ├── ELEC491_Final_Report.pdf          # full design, analysis and results
    ├── ELEC491_Poster.pdf
    ├── ELEC491_Proposal.pdf
    └── images/
```

## Building the firmware

The firmware targets ESP-IDF (ESP32-WROOM-32U). It uses an HY-SRF05 ultrasonic sensor driver
(`ultrasonic.h`) and an SH1106 OLED display wrapper based on the lcdgfx library; add those two
components to the ESP-IDF project before building.

---

**Author:** Yaşar Aykut Altuntop · MSc student, Biomedical Sciences and Engineering, Koç University  
[LinkedIn](https://linkedin.com/in/aykutaltuntop) · [GitHub](https://github.com/Aykut0)
