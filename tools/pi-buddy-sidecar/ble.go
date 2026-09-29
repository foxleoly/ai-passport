package main

/*
#cgo CFLAGS: -x objective-c
#cgo LDFLAGS: -framework CoreBluetooth -framework Foundation
#include <stdint.h>
void *pb_new(void);
int    pb_scan(void *c);
void   pb_write_rx(void *c, const uint8_t *data, int len);
int    pb_state(void *c);
int    pb_poll(void *c, int *kind, int *state, uint8_t *data, int cap, int *datalen, int timeout_ms);
void   pb_release(void *c);
*/
import "C"

import "unsafe"

// BLE event kinds (mirror the ObjC side).
const (
	pbEvState      = 1
	pbEvConnected  = 2
	pbEvDisconn    = 3
	pbEvTxData     = 4
	pbEvDiscovered = 5
	// New-SDK CBManagerState values (not the legacy CBCentralManagerState).
	cbStatePoweredOn    = 5
	cbStatePoweredOff   = 4
	cbStateUnauthorized = 3
	cbStateUnsupported  = 2
)

// Ble wraps the CoreBluetooth NUS central.
type Ble struct {
	h unsafe.Pointer
}

// BleEvent is one polled event.
type BleEvent struct {
	Kind    int
	State   int
	Data    []byte
	Timeout bool
}

func BleNew() *Ble {
	return &Ble{h: C.pb_new()}
}

func (b *Ble) Scan() bool {
	return int(C.pb_scan(b.h)) == 0
}

func (b *Ble) State() int {
	return int(C.pb_state(b.h))
}

func (b *Ble) WriteRX(data []byte) {
	if len(data) == 0 || b.h == nil {
		return
	}
	C.pb_write_rx(b.h, (*C.uint8_t)(unsafe.Pointer(&data[0])), C.int(len(data)))
}

// Poll blocks up to timeoutMs for the next event.
func (b *Ble) Poll(timeoutMs int) BleEvent {
	var kind, state, dlen C.int
	buf := make([]byte, 600)
	rc := int(C.pb_poll(b.h, &kind, &state, (*C.uint8_t)(unsafe.Pointer(&buf[0])),
		C.int(len(buf)), &dlen, C.int(timeoutMs)))
	ev := BleEvent{Kind: int(kind), State: int(state), Timeout: rc == 0}
	if rc == 1 {
		ev.Data = buf[:int(dlen)]
	}
	return ev
}
