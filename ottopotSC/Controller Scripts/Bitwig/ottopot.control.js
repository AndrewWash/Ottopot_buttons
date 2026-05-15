loadAPI(18);

host.setShouldFailOnDeprecatedUse(true);
host.defineController(
	"gerotakke",
	"ottopot",
	"1.1",
	"07f84ffa-b07c-46d6-9789-9a08c0ecdc3e",
	"gerotakke",
);
host.defineMidiPorts(1, 1);
host.addDeviceNameBasedDiscoveryPair(["Ottopot"], ["Ottopot"]);
host.addDeviceNameBasedDiscoveryPair(["Ottopot MIDI 1"], ["Ottopot MIDI 1"]);


let colorOff = [
	[0, 0, 0],
	[0, 0, 0],
	[0, 0, 0],
	[0, 0, 0],
	[0, 0, 0],
	[0, 0, 0],
	[0, 0, 0],
	[0, 0, 0],
];
let colorOn = [60, 50, 127];

// Uncomment this next section to use the Bitwig remote colors for the LED rings
// colorOff = [
// 	[15, 0, 0],
// 	[20, 8, 0],
// 	[25, 12, 0],
// 	[0, 12, 0],
// 	[8, 12, 9],
// 	[8, 0, 15],
// 	[22, 0, 22],
// 	[25, 0, 12],
// ];
// colorOn = [127, 127, 127];

let values = [0, 0, 0, 0, 0, 0, 0, 0];
let remoteControlCursor;
let outPort;
let sendUpdates = [];

function init() {
	let inPort = host.getMidiInPort(0);
	outPort = host.getMidiOutPort(0);
	inPort.setMidiCallback(onMidi0);

	let cursorTrack = host.createCursorTrack(0, 0);
	let cursorDevice = cursorTrack.createCursorDevice();
	remoteControlCursor = cursorDevice.createCursorRemoteControlsPage(8);

	for (let j = 0; j < remoteControlCursor.getParameterCount(); j++) {
		let valueFn = onValueChange.bind(this, j);
		let param = remoteControlCursor.getParameter(j);
		param.markInterested();
		param.setIndication(true);
		param.value().addValueObserver(16384, valueFn);
	}

	for (let i = 0; i <= 7; i++) {
		outPort.sendMidi(0xb0 + i, 101, colorOn[0]);
		outPort.sendMidi(0xb0 + i, 102, colorOn[1]);
		outPort.sendMidi(0xb0 + i, 103, colorOn[2]);
		outPort.sendMidi(0xb0 + i, 104, colorOff[i][0]);
		outPort.sendMidi(0xb0 + i, 105, colorOff[i][1]);
		outPort.sendMidi(0xb0 + i, 106, colorOff[i][2]);
	}

	println("ottopot initialized!");
}

function onValueChange(paramIdx, value) {
	sendUpdates.push({
		paramIdx: paramIdx,
		value: value,
	});
}

function onMidi0(status, data1, data2) {
	// printMidi(status, data1, data2);
	let paramIdx;

	if (isChannelController(status)) {
		if (data1 >= 9 && data1 <= 16) {
			paramIdx = data1 - 9;
			values[paramIdx] = data2 << 7;
			remoteControlCursor
				.getParameter(paramIdx)
				.value()
				.set(values[paramIdx] / 16384);
		}
		if (data1 >= 41 && data1 <= 49) {
			paramIdx = data1 - 41;
			values[paramIdx] += data2;
			remoteControlCursor
				.getParameter(paramIdx)
				.value()
				.set(values[paramIdx] / 16384);
		}
	}
	return true;
}

function flush() {
	for (let update of sendUpdates) {
		let hsb = update.value >> 7;
		let lsb = update.value & 127;
		outPort.sendMidi(0xb5, 9 + update.paramIdx, hsb);
		outPort.sendMidi(0xb5, 9 + update.paramIdx + 32, lsb);
	}
	sendUpdates = [];
}

function exit() {}
