loadAPI(18);

host.setShouldFailOnDeprecatedUse(true);
host.defineController(
	"gerotakke",
	"ottopot",
	"0.1",
	"07f84ffa-b07c-46d6-9789-9a08c0ecdc3e",
	"gerotakke",
);
host.defineMidiPorts(1, 1);
host.addDeviceNameBasedDiscoveryPair(["Ottopot"], ["Ottopot"]);

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
			remoteControlCursor.getParameter(paramIdx).value().set(values[paramIdx] / 16384);
		}
		if (data1 >= 41 && data1 <= 49) {
			paramIdx = data1 - 41;
			values[paramIdx] += data2;
			remoteControlCursor.getParameter(paramIdx).value().set(values[paramIdx] / 16384);
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
