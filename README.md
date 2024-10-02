# BoostedUnbrickFirmware
Custom dsPIC33 firmware for the Boosted Extended Range (XR) Battery to emulate necessary functions for operating the skateboard.

## Disclaimer
Lithium batteries can be very dangerous if improperly handled. I am providing the results of my experiments AS-IS. There may be issues still with it operating, use this at your own risk. I take no responsibility for any damages caused by using this firmware on your board. Using this firmware will erase the normal boosted firmware on your MCU, this is not reversible.

## Introduction
### Background
About two years ago, my brother had picked up a Boosted Mini X off of Craigslist for ~$100. The board had the infamous Red-Light-Of-Death (RLOD), which is usually caused by the lithium cells becoming unbalanced. RLOD is a firmware lockout of the battery's operation and is permanent unless you get it repaired by boosted. Nonetheless, with Boosted now out of business, and second-hand batteries becoming expensive on eBay and other sites, I wanted to see if I could revive the battery I already had. 

### Adventure 1: Uninvasive Repairs - Clearing RLOD
I managed to get the pack cracked open and measured each of the cells. As expected, a few of the cells were quite low from years of sitting in the previous owner's shed. I manually balanced the cells using a bench power supply, but the RLOD persisted, even though the cells were within 50mV of each other. I then found [jonataubert's RLOD B2XR guide](https://github.com/jonataubert/RLOD_B2XR), which notes that the RLOD is stored in an onboard SPI flash. I followed the instructions for resetting the data in the battery's onboard flash memory using flashrom on a Raspberry Pi. This did manage to be successful once, but the RLOD showed up again after some time. In having some busy final semesters in college, I ended up shelving the project with the debug wires still attached to the delicate SPI flash. At some point, I accidentally bumped one of the 3.3V data lines into the 50V main battery terminal... and poof goes the magic smoke (as my college embedded systems professor called it). The overvolted data line caused the attached SPI flash and the main dsPIC33 MCU to have their lines overvolted and rendered unusable. No more simple flash modification for me.

### Adventure 2: Shooting in the Dark - Boosted SRB Firmware Emulation on a PIC
With the magic fuzzies let out of my dsPIC33 and ISLP25 SPI Flash, I was out of options for just fixing the RLOD data. The only option would be to control the hardware on the board using another MCU. Initially I thought about making my own PCB with an Arduino, but I did some research into the PIC chip and found that Microchip has an easy to use development environment (MPLAB X IDE). The MCU which Boosted used in the XRB is the dsPIC33EP512GP504, and goes for about $5 on [DigiKey](https://www.digikey.com/en/products/detail/microchip-technology/DSPIC33EP512GP504-I-PT/3879834), so I figured I'd try my luck in programming one of those. With the fried MCU desoldered from the battery PCB, I was able to use a multimeter to figure out where the pins routed to and an overall circuit diagram for how the MCU is hooked up. Now that the beep-beep of continuity mode on the multimeter was out of the way, I used MPLAB X and the MCC to configure the pins in the software to match the PCB pads. Some more days and cups of coffee later, I was able to talk to the Battery Management System (BMS) IC over I2C, and control charging and discharging. However, I found the Electronic Speed Controller (ESC) requires CAN Bus data from the battery to enable operation. Thankfully, I stumbled across [rscullin's](https://github.com/rscullin) [BeamBreak](https://github.com/rscullin/beambreak) guide which had a Python script for emulating the basic CAN Bus packets the standard range battery (SRB). I managed to get that converted to use the CAN peripheral on the PIC and added that to the BMS. Profit. Now the motors were able to do some simple spin tests. Emulation of the SRB and basic battery management made for a nice joyride on the now partially functional board.

### Adventure 3: Tuning All the Knobs - Boosted XRB Firmware Emulation on a PIC
With a somewhat rideable board under my feet, I wanted to get everything tuned nicely to behave like the old battery did. Unfortunately, this mean more digging in the dark to figure out how to emulate the XRB CAN Bus. Fortunately, on of my peers from college happened to have a functional Boosted Mini X with a XRB. He graciously agreed to let me borrow it for some ~~evil scheming~~ *productive analysis of the XRB CAN Frames*. I used my DIY CAN Bus reader to sit on the CAN Bus line and print out all of the messages going between his functional XRB and my ESC (which had tap wires soldered to it). I got a nice dump of frames to sort through and understand. I stumbled across yet another great project, the [BoostedBreak](https://github.com/axkrysl47/BoostedBreak) project by [axkrysl47](https://github.com/axkrysl47), which had interpretation for the frames the XRB and ESC were sending to each other. After a few iterations and tuning the timing, the custom firmware for the dsPIC33 was emulating the XRB frames close enough to have the ESC not signal an error (cause the remote to wail at the top of its lungs). It's still a work in progress, but this firmware should be able to replace the original XRB firmware and allow a once-RLOD'd board to work once again. I still have some features of the XRB that need to be implemented, such as the button press codes and the remote poweroff, but it's brought life back to this board that's been collecting dust. I have some more of the technical details to come in the [Deep Dive](#deep-dive) section of the README in a future release.

## A Huge Thanks To
- [rscullin](https://github.com/rscullin) and the [BeamBreak](https://github.com/rscullin/beambreak) project, which helped get initial SRB emulation working.
- [axkrysl47](https://github.com/axkrysl47) and the [BoostedBreak](https://github.com/axkrysl47/BoostedBreak) project which helped in interpreting the CAN Bus frames sent between the battery and ESC.
- [jonataubert](https://github.com/jonataubert) and the [RLOD B2XR](https://github.com/jonataubert/RLOD_B2XR) project which helped in understanding the symptoms of RLOD.
- [r/Lambertofmtl](https://www.reddit.com/r/boostedboards/comments/ghdyi7/bb_v2_xr_bms_pcb_analysis/) and the [B2XR PCB Analysis](https://www.reddit.com/r/boostedboards/comments/ghdyi7/bb_v2_xr_bms_pcb_analysis/) Reddit post which helped in finding datasheets for the hardware.

## Tools Used for this Project
- MPLAB X IDE with xc16 Compiler
- PICKit3 In-circuit Debugger
- Lots of patience

## Custom Firmware Features
### Normal Boosted XRB Functions
- State of Charge (SoC) reading on XRB and remote
- Cell balancing
- Overvoltage/Undervoltage protection
- CAN Bus communication for ESC operation

### Custom Features
- Min/Max cell delta on 5-segment display when charging
- Limp mode (temporarily lowers minimum cell voltage)

## To-Do
- Automatic power off via remote
- BMS automatic re-enable after charge completion (must be power cycled before riding)
- Button press CAN Bus functionality (button only turns on/off right now)

## Not functional
- Reading the SPI flash
- Many things stored by the memory (I'm not sure what's stored here as my memory chip let out magic smoke)

## Deep Dive
To-Do.
### Observing the Hardware

### Software Setup

### CAN Bus