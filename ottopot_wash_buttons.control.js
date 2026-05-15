// Ottopot Wash + Buttons — Bitwig Studio controller extension for the Ottopot
// 8-knob 14-bit MIDI controller with two added momentary switches wired to
// Teensy pins 2 (Prev Bank) and 3 (Next Bank). The switches transmit
// CC 110 / CC 111 on MIDI channel 6 from the firmware and are bound here
// to the cursor device's Remote Controls page select prev/next actions.
//
// Loads via the HardwareSurface API (Bitwig 3.1+). The original
// "ottopot_wash.control.js" is preserved as a fallback — load one OR the
// other in Bitwig's Controllers settings.

loadAPI(18);
host.setShouldFailOnDeprecatedUse(true);

host.defineController(
    "wash_buttons",
    "Ottopot Wash + Buttons",
    "0.2",
    "7a3f9c2e-1b8d-4f6a-9e5c-3d2a1b7f0e8c",
    "wash_buttons"
);
host.defineMidiPorts(1, 1);
host.addDeviceNameBasedDiscoveryPair(["Ottopot"], ["Ottopot"]);

const KNOB_COUNT = 8;
// Incoming MIDI: match any channel (the original script used isChannelController,
// which accepts any channel — replicating that here keeps the hardware working
// regardless of which channel it actually transmits on).
// Outgoing MIDI: channel 6, matching the original script's 0xB5 status byte.
const OUTPUT_CHANNEL = 5;                     // 0-indexed -> channel 6
const MSB_CC_BASE = 9;                        // MSB CCs: 9..16
const LSB_CC_OFFSET = 32;                     // LSB CCs: MSB + 32 -> 41..48
const VALUE_RESOLUTION = 16384;               // 14-bit
const CC_STATUS = 0xB0 | OUTPUT_CHANNEL;

// Switches (matches firmware: pins 2/3 -> CC 110/111 on channel 6).
const SW_CHANNEL   = 5;                       // 0-indexed -> channel 6
const CC_PREV_BANK = 110;
const CC_NEXT_BANK = 111;

let midiOut = null;
let remotePage = null;
const observedValues = new Array(KNOB_COUNT).fill(0);
const lastSentValues = new Array(KNOB_COUNT).fill(-1);
const dirty = new Array(KNOB_COUNT).fill(true);

function init() {
    const midiIn = host.getMidiInPort(0);
    midiOut = host.getMidiOutPort(0);
    const surface = host.createHardwareSurface();

    const cursorTrack = host.createCursorTrack(0, 0);
    const cursorDevice = cursorTrack.createCursorDevice();
    remotePage = cursorDevice.createCursorRemoteControlsPage(KNOB_COUNT);

    for (let i = 0; i < KNOB_COUNT; i++) {
        const msbCC = MSB_CC_BASE + i;
        const lsbCC = msbCC + LSB_CC_OFFSET;

        const msbMatcher = midiIn.createAbsoluteCCValueMatcher(msbCC);
        const lsbMatcher = midiIn.createAbsoluteCCValueMatcher(lsbCC);
        const pairedMatcher = midiIn.createSequencedValueMatcher(msbMatcher, lsbMatcher, false);

        const knob = surface.createAbsoluteHardwareKnob("ottopot_knob_" + i);
        knob.setAdjustValueMatcher(pairedMatcher);

        const param = remotePage.getParameter(i);
        param.setIndication(true);
        knob.setBinding(param);

        const knobIndex = i;
        param.value().addValueObserver(VALUE_RESOLUTION, function (value) {
            observedValues[knobIndex] = value;
            dirty[knobIndex] = true;
        });
    }

    // Bank prev/next switches.
    const btnPrev = surface.createHardwareButton("ottopot_btn_prev_bank");
    btnPrev.pressedAction().setActionMatcher(
        midiIn.createCCActionMatcher(SW_CHANNEL, CC_PREV_BANK, 127)
    );
    btnPrev.pressedAction().setBinding(remotePage.selectPreviousAction());

    const btnNext = surface.createHardwareButton("ottopot_btn_next_bank");
    btnNext.pressedAction().setActionMatcher(
        midiIn.createCCActionMatcher(SW_CHANNEL, CC_NEXT_BANK, 127)
    );
    btnNext.pressedAction().setBinding(remotePage.selectNextAction());

    println("ottopot_wash_buttons ready: " + KNOB_COUNT + " knobs + 2 bank buttons, in=any channel, out=ch " + (OUTPUT_CHANNEL + 1));
}

function flush() {
    if (midiOut === null) return;
    for (let i = 0; i < KNOB_COUNT; i++) {
        if (!dirty[i]) continue;
        const value = observedValues[i];
        if (value !== lastSentValues[i]) {
            const msb = (value >> 7) & 0x7F;
            const lsb = value & 0x7F;
            midiOut.sendMidi(CC_STATUS, MSB_CC_BASE + i, msb);
            midiOut.sendMidi(CC_STATUS, MSB_CC_BASE + i + LSB_CC_OFFSET, lsb);
            lastSentValues[i] = value;
        }
        dirty[i] = false;
    }
}

function exit() {
    if (remotePage === null) return;
    for (let i = 0; i < KNOB_COUNT; i++) {
        remotePage.getParameter(i).setIndication(false);
    }
}
