-- This file needs to be saved as ~/Music/Audio Music Apps/MIDI Device Scripts/Teensyduino/Ottopot.device/config.lua for Logic to pick it up.
-- Logic should recognize the Ottopot as a control surface on startup if this file is in the correct spot.

function controller_midi_out(midiEvent,name,valueString,color)
  -- It appears that Logic's implementation of sending 14bit CCs back to the controller is pretty badly broken.
  -- As best as I can tell, it sends a set of 2 midiEvents in one with 2 CCs where the first data byte is always
  -- 53 and the second is the CC of the LSB but actually contains the MSB in the data byte. Sending the MSB as
  -- both data bytes is the best I can do without any proper documentation or debugging tools right now.

	if midiEvent[0] == 0xB5 then
		receivedEvents = true
		if (midiEvent[1] >= 0x09 and midiEvent[1] <= 0x10) then
			return {midi={0xB5, midiEvent[1], midiEvent[5], 0xB5, midiEvent[1] + 32, midiEvent[5]}}
		end

	end
	return nil
end

-- Define the knobs, buttons, etc. of the device
-- This is also used to check if this device is a match (model, manufacturer and/or device inquiry status)
function controller_info()
	return {
		-- model name for this device
		model = 'Ottopot',
		-- manufacturer name for this device
		manufacturer = 'Teensyduino',

		-- Certain controllers are passed through automatically (Pitch Bend, Modulation, etc)
		auto_passthrough = false,

		-- All buttons, knobs, keyboard, possible pedals are defined here
		items = {
			-- FADERS
			{name = 'Knob 1', objectType = 'Knob', midi = {0xB5, 0x09, MIDI_MSB, 0xB5, 0x29, MIDI_LSB}},
			{name = 'Knob 2', objectType = 'Knob', midi = {0xB5, 0x0A, MIDI_MSB, 0xB5, 0x2A, MIDI_LSB}},
			{name = 'Knob 3', objectType = 'Knob', midi = {0xB5, 0x0B, MIDI_MSB, 0xB5, 0x2B, MIDI_LSB}},
			{name = 'Knob 4', objectType = 'Knob', midi = {0xB5, 0x0C, MIDI_MSB, 0xB5, 0x2C, MIDI_LSB}},
			{name = 'Knob 5', objectType = 'Knob', midi = {0xB5, 0x0D, MIDI_MSB, 0xB5, 0x2D, MIDI_LSB}},
			{name = 'Knob 6', objectType = 'Knob', midi = {0xB5, 0x0E, MIDI_MSB, 0xB5, 0x2E, MIDI_LSB}},
			{name = 'Knob 7', objectType = 'Knob', midi = {0xB5, 0x0F, MIDI_MSB, 0xB5, 0x2F, MIDI_LSB}},
			{name = 'Knob 8', objectType = 'Knob', midi = {0xB5, 0x10, MIDI_MSB, 0xB5, 0x30, MIDI_LSB}},
		}
	}
end
