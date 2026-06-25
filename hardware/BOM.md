# Bill of Materials

| Ref | Qty | Description                          | Value / Part number     | Package      | Source hint   |
|-----|-----|--------------------------------------|-------------------------|--------------|---------------|
| U1  | 1   | Microcontroller board                | Raspberry Pi Pico (RP2040) | 2.54 mm headers | Pimoroni, Adafruit |
| U2  | 1   | USB 2.0 2:1 bidirectional switch     | TI TS3USB221DRCR (VSON) or TS3USB221RSER (UQFN) | VSON (DRC) 10-pin or UQFN (RSE) 10-pin | Mouser, LCSC |
| Q1  | 1   | NPN transistor                       | BC547 or 2N2222         | TO-92        | Any           |
| J1  | 1   | USB Type-A Female connector (input)  | —                       | Through-hole | LCSC C404965  |
| J2  | 1   | USB Type-A Male connector or cable   | —                       | —            | Breakout module |
| J3  | 1   | 2-pin Dupont/Berg header             | —                       | 2.54 mm      | Any           |
| J4  | 1   | 2-pin power input header (5VSB)      | —                       | 2.54 mm      | Any           |
| R1  | 1   | Resistor, transistor base            | 1 kΩ, 1/4 W            | 0805 or axial | Any          |
| R2  | 1   | Resistor, VBUS divider top           | 10 kΩ, 1/4 W           | 0805 or axial | Any          |
| R3  | 1   | Resistor, VBUS divider bottom        | 10 kΩ, 1/4 W           | 0805 or axial | Any          |
| F1  | 1   | Polyfuse / resettable fuse           | 500 mA hold, 5 V        | 1812         | Mouser, LCSC  |
| C1  | 1   | Decoupling cap, U2 VCC               | 100 nF, 10 V, X7R       | 0402 or 0805 | Any           |
| C2  | 1   | Bulk cap, 5VSB rail                  | 47 µF, 10 V             | Electrolytic | Any           |

## Optional

| Ref | Qty | Description          | Notes                                    |
|-----|-----|----------------------|------------------------------------------|
| D1  | 1   | Status LED + 330 Ω R | Wired to GP16; solid=passthrough, slow blink=monitoring, fast blink=powering on |
| SW1 | 1   | Reset button         | Connected to Pico RUN pin for easier reflashing |

## Approximate cost (2025 single-unit pricing)

| Item            | ~USD |
|-----------------|------|
| Raspberry Pi Pico | $4 |
| TS3USB221       | $1   |
| Passives + Q1   | $1   |
| Connectors      | $2   |
| **Total**       | **~$8** |
