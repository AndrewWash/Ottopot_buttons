# ottopot MIDI Controller
Version 1.1
License: GPL v3 (http://www.gnu.org/licenses/gpl.html)
https://gerotakke.de/ottopot

The ottopot is a MIDI controller with 8 endless potentiometers. It sends 14bit CCs and tries to mimic the feel of
“regular analog potentiometers” as good as possible. It uses Duppa's small LED rings to indicate the current value
of each pot (optional). MIDI feedback is supported so changes of the digital value in the DAW will be reflected by the LEDS.
It's aimed to control Bitwig's Remote Controls or Logic's Smart Controls.

The case is fully 3D printed, Blender files are in this repo and print files are on printables.com.
Build guide and material lists are on https://gerotakke.de/ottopot


### Build

If you want to just update to the newest version, download [the current firmware.hex file](https://codeberg.org/gerotakke/ottopot/releases) and flash it to the Teensy using [tytools](https://github.com/Koromix/tytools) TyUploader.

The code is a [platformIO](https://platformio.org/){target="_blank"} project. I'm using platformIO core from the command line but
the VSCode extension should be fine as well.  
Clone the [project from the repo](https://codeberg.org/gerotakke/ottopot){target="_blank"} to your computer.  
If you have platformIO set up, make sure you connect the Teensy to your computer via USB and be sure that it is the only Teensy that is connected. Then run `pio run --target upload` from the main directory of the repo and that should be it.  

For Linux users, I've found that only [tytools](https://github.com/Koromix/tytools) can flash the Teensy without having to press the reset button
on the board. I have a commented region in `platformio.ini` with the settings for that.  
It also seems to be the best way to handle flashing when you have multiple Teensys connected.
