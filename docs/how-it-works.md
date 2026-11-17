# How Tourelle works

This page tells how a 2008 motorized webcam got its pan/tilt back on Windows
11. The investigation took four steps. Each one ruled out an easy fix.

## 1. The symptom

Plugged into Windows 11, the camera (USB `046D:0994`) loads Windows' generic
UVC driver, `usbvideo.sys`. The video works. The motorized head never moves:
no application shows pan or tilt controls, and the original vendor software no
longer runs on Windows 11.

## 2. What Windows sees

The first probe lists the camera's topology through DirectShow
(`IKsTopologyInfo`) and reads every extension unit control through
`IKsControl`, without writing anything:

| Unit | GUID | What it holds |
|---|---|---|
| 4 | `…8D221E` | read-only device info (its selector 6 returns `046D 0994 0008`) |
| 13 | `…8D221F` | LED control |
| 10 | `…8D2252` | 24 internal registers, some reading `BEEF` |

The Camera Terminal advertises no pan, tilt or focus control (`bmControls 0E 00 00`).
Open-source Linux tools drive the motors through a *Motor Control* unit, GUID
`…8D2256`, but that unit is nowhere in this list.

## 3. The hidden unit

Asking the USB hub for the raw configuration descriptor
(`IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION`) answers the question. Next to
the standard class-specific descriptors (type `0x24`), the camera declares four
more extension units with a **vendor descriptor type, `0x41`**:

```
20 41 01 09  82 06 61 63 70 50 AB 49 B8 CC B3 85 5E 8D 22 56  05 01 04 03 1F 00 00 ...
│  │     │   └────────── GUID …8D2256 (Motor Control) ───────┘  │  │  │  │  └ bmControls: selectors 1-5
│  │     │                                                     │  │  │  └ bControlSize
│  │     unit id 9                                             │  │  └ source id
│  vendor type 0x41 instead of 0x24 (CS_INTERFACE)             │  └ bNrInPins
bLength                                                        bNumControls = 5
```

A generic UVC driver skips descriptors whose type it does not know, so
`usbvideo.sys` never creates a node for unit 9. The camera still answers
requests addressed to that unit. Windows simply has no path to send them.

Once the requests do reach the unit, its controls read:

| Selector | Size | GET_INFO | Meaning |
|---|---|---|---|
| 1 | 4 | `0x13` (get, set, asynchronous) | pan/tilt relative: `int16 pan, int16 tilt`, unit 1/64° |
| 2 | 1 | `0x12` (set, asynchronous) | reset: bit 0 pan, bit 1 tilt |
| 3 | 6 | `0x03` | focus motor |

GET_MIN and GET_MAX of selector 1 give pan ±4480 and tilt ±1920, which is ±70° and ±30°.

## 4. Reaching the unit

A motor command is a plain UVC class request:

```
SET_CUR  bmRequestType 0x21  bRequest 0x01
         wValue = selector << 8      wIndex = (unit 9 << 8) | interface 0
```

Only the driver that owns interface 0 can send it. Two dead ends came first:

- **WinUSB on the audio interface.** It talks to the camera, but the composite
  driver rewrites the interface number in `wIndex`. Every request then lands on
  the audio interface, even requests to units that normally answer.
- **Vendor requests.** The camera answers them, but with a constant status, not UVC data.

The way that works is to bind **WinUSB to the video function** (interfaces 0 and
1). The motors then move. The price is that `usbvideo.sys` is gone, so Tourelle
has to produce the video itself.

## 5. A user-mode UVC driver

`native/uvc/UvcDevice.cpp` does the class driver's job over WinUSB:

1. It parses the configuration descriptor: formats, frame sizes, intervals, the isochronous endpoint.
2. It runs the PROBE/COMMIT negotiation to choose YUY2 640×480 at 30 fps.
3. It picks the smallest alternate setting whose bandwidth covers `dwMaxPayloadTransferSize`.
4. It streams with `WinUsb_ReadIsochPipeAsap`: 4 transfers of 64 packets in
   flight, so a high-speed transfer is always a multiple of 8 packets.
5. It rebuilds frames from the UVC payload headers (FID toggle, EOF and ERR bits).
6. It sets the auto-exposure priority to 0. In low light the camera otherwise
   drops to about 6 fps to expose longer.

Result: 30 fps with no dropped frame, while the head moves.

## 6. Back into every application: a virtual camera

Windows 11 can publish a camera implemented as a Media Foundation source
(`MFCreateVirtualCamera`). `TourelleCamera.dll` is that source. The Windows
Camera Frame Server loads it, so every application that lists cameras sees
**"Tourelle Camera"**. Several applications can share it.

- The stream converts YUY2 to NV12, or passes YUY2 through, one sample per `RequestSample`.
- The source owns the WinUSB handle while streaming. It opens the camera only briefly for a motor command when idle.
- Motor commands come in through `IKsControl` on a private property set. Any
  client can send them through the camera's media source, and `tourelle.exe`
  does exactly that.

## Tools used

- `tools/dumpdesc.ps1`: raw descriptor dump, including vendor `0x41` descriptors.
- `native/tools/uvcgrab.cpp`: stand-alone stream and motor test.
- Microsoft's documentation for the Frame Server custom media source, and the
  Windows-Camera *VirtualCamera* sample, for the interfaces a source must implement.
