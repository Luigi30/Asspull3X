# Asspull IIIx Memory Map
*Insert some W3C-like crap here...*

## Address space
Address space is effectively 28 bits:

    $0FFFFFFF
      |_______ bank

## Regions
| from     | to       | size     | name
| -------- | -------- | -------- |------
| 00000000 | 0001FFFF | 00020000 | BIOS
| 00020000 | 00FFFFFF | 00FE0000 | ROM
| 01000000 | 01400000 | 00400000 | RAM
| 013F0000 | 0FFFFFFF | 000FFFFF | STACK
| 02000000 | 02080000 | 00080000 | DEV
| 0D800000 |          |          | IO
| 0E000000 | 0EFFFFFF | 00FFFFFF | VRAM

## VRAM
| from     | content
| -------- |---------
| 0E000000 | Text/Bitmap/Tilemaps
| 0E010000 | Tileset
| 0E050000 | Palette
| 0E050200 | Font
| 0E054000 | Sprite tile/pal
| 0E054200 | Sprite position/flip/priority

### Size considerations
* text: up to 80×60=4800 16-bit cells: 4800×2 = 9600 => `$02580`
* bitmap: up to 640×480=307200 8-bit pixels => `$4B000`
* tilemap: 64×64=4096  16-bit cells: 4096×2 = 8192 => `$02000`
  * ...times 4 = 8192×4 => `$08000`
* tileset: 8×8 4bpp cells = 32, times 2048 or so = 1024 => `$08000`
* palette: up to 256 16-bit colors: 256×2 = 512 => `$00200`
* font: 12288 bytes => `$03000`
* sprites idea A: up to 256 16-bit entries = 512 => `$00200`
* sprites idea B: up to 256 32-bit entries = 1024 => `$00400`

## Tilemap cell
    PPPP vh.T TTTT TTTT
    |    || |____________ Tile #
    |    ||______________ Horizontal flip
    |    |_______________ Vertical flip
    |____________________ Palette #

## Sprite
    PPPP EBBT TTTT TTTT
    |    || |____________ Tile #
    |    ||______________ Blend mode (0 off, 1 add, 2 subtract)
    |    |_______________ Enabled
    |____________________ Palette #
    PPP2 vhyx ...V VVVV VVVV ..HH HHHH HHHH
    |  | ||||    |             |___________  Horizontal position
    |  | ||||    |_________________________  Vertical position
    |  | ||||______________________________  Double width
    |  | |||_______________________________  Double height
    |  | ||________________________________  Horizontal flip
    |  | |_________________________________  Vertical flip
    |  |___________________________________  Double width and height
    |______________________________________  Priority

## Draw order

### Tilemap mode
1. Backdrop
2. Sprites with priority 4
3. Map 0
4. Sprites with priority 3 
5. Map 1
6. Sprites with priority 2
7. Map 2
8. Sprites with priority 1
9. Map 3
10. Sprites with priority 0

### Other modes:
11. Stuff
12. Sprites with any priority

---

## Device I/O
Each of the sixteen blocks of `8000` bytes starting from `2000000` may or may not map to a device. The first two bytes of each block identify what kind of device it is. If those bytes are the value `0000` or `FFFF` there is no device.

### Disk drive
The disk drive is identified by the value `0144`.  The next `uint16` value selects which sector to read or write (IO register `00030` before) and the next byte controls the device (`00032` before):

    ...B WREP
       | ||||__ Disk present (read only)
       | |||___ Error state (read only)
       | ||____ Read now
       | |_____ Write now
       |_______ Busy state (read only)

From the 512th byte on, another 512 bytes form the disk controller's internal RAM, used to hold a sector's worth of data to read or write. That leaves plenty room for expansion.

### Line printer
Identified by the value `4C50`, writing to the next byte pipes directly to the printer. Reading it returns `00` or an error value, to be determined. *This might make a nice alternative to `REG_DEBUGOUT`...*

---

## Register map

### 00000	Line
The current line being drawn as a `uint16`.
### 00002	TilemapBlend
    SSSS EEEE
    |    |_____ Enable for these layers
    |__________ Subtract instead of add
*Yeah, just... stick in there why don't you. You monster.*
### 00003	ScreenMode
### 00003	ScreenMode
    B32. ..MM
    |||    |___ Mode
    |||________ 240px tall instead of 480px
    ||_________ 320px wide instead of 640px
    |__________ Bold in text mode, 200 or 400px in bitmap mode
### 00004	ScreenFade
    W..A AAAA
    |  |_______ Amount
    |__________ To white
### 00005	Interrupts
    X... .VH.
    |     ||___ HBlank triggered
    |     |____ VBlank triggered
    |__________ Disable interrupts
Used by the BIOS dispatcher to determine what to call. Applications can set the disable bit to prevent them from firing.
### 00006	KeyIn
    ..... .CAS KKKK KKKK
           ||| |_____ Keycode
           |||_______ Shift
           ||________ Alt
           |_________ Control
### 00008	TickCount
The amount of ticks since the system was turned on as a `uint32`.
### 0000C	Joypad1
    YXBA RLDU
### 0000D	Joypad2
### 0000E	DebugOut
Pipe characters to `STDOUT`. 
### 0000F	TilemapSet
    EEEE TTTT
    |||| | |___ Tile shift for layers 1 and 3
    |||| |_____ Tile shift for layer 2 and 4
    ||||_______ Layer 1 enabled
    |||________ Layer 2 enabled
    ||_________ Layer 3 enabled
    |__________ Layer 4 enabled
Tile shift adds `128 << T` to the tile # when rendering, so a shift value of 3 means a whole separate set of 1024 tiles.
### 00010	TilemapScrollH1
### 00012	TilemapScrollV1
Scroll values for the tile map as `int16`. This repeats for each of the four layers up to `001E TilemapScrollV4`. *The tile map controls are a work in progress.*
### 00020	DMASource
Either a pointer or a value.
### 00024	DMATarget
Certainly a pointer.
### 00028	DMALength
Certainly a value.
### 0002A	DMAControl
    ..WW VTSE
      |  ||||__ Enable now
      |  |||___ Increase source every loop
      |  ||____ Increase target every loop
      |  |_____ Use source as direct value, not as a pointer
      |________ Width of data to copy
#### 00040	MidiOut
Send a raw 32-bit message through the MIDI OUT port.
### 00080	HDMAControl1
    ...L LLLL LLLL ...S SSSS SSSS D.WW ...E
       |              |           | |     |__ Enable
       |              |           | |________ Width of data to copy
       |              |           |__________ Doublescan
       |              |______________________ Starting scanline
       |_____________________________________ Linecount
This repeats until `0009C HDMAControl8`.
### 000A0	HDMASource1
A pointer. This too repeats until `000BC HDMASource8`.
### 000C0	HDMADest1
A pointer. Again repeated until `000DC HDMADest8`.

---

## Blitter
| register | name        | format                                    |
| -------- | ----------- | ----------------------------------------- |
| 00100    | Function    | `.... .... .... .... .... .... .... FFFF` |
| 00104    | Source      | Pointer or value                          |
| 00108    | Destination | Pointer                                   |
| 0010C    | Length      | Value                                     |
| 00110    | Key         | Value                                     |

### 0001	Blit
    2222 2222 2222 1111 1111 1111 44KS 0001
    |              |              ||||_______ Enable strideskip
    |              |              |||________ Enable colorkey
    |              |              ||_________ Source is 4-bits
    |              |              |__________ Target is 4-bits
    |              |_________________________ Source stride
    |________________________________________ Target stride
* Copies `length` bytes from `source` to `dest`. If `strideskip` is enabled, copies from `source` for `stride` bytes, then skips over `target stride - source stride` bytes, until `length` bytes are copied in total.
* If `colorkey` is enabled, pixels matching `key` are skipped.
* If `4-bit source` is set:
  * If `4-bit target` is set, care is taken to work on nibbles.
  * If `4-bit target` is *not* set, the source data is expanded.
* If `4-bit source` is not set:
	* If `4-bit target` is set, behavior is undefined.
	* If `4-bit target` not set, nothing special is done.

### 0010	Clear
    2222 2222 2222 1111 1111 1111 .WWS 0010
    |_________ Source width
* Copies the value of `source` to `dest`.
* If `width` is set to 0, sets `dest` to the low byte of the source value.
* If `width` is set to 1, sets `dest` to the lower short instead.
* If `width` is set to 2, sets `dest` to the full word.
* If `width` is set to 3, behavior is undefined.

### 0011	Invert
    2222 2222 2222 1111 1111 1111 ...S 0011
Inverts the byte values at B, simple as that.

### 0100	UnRLE
Performs a simple RLE decompression from `source` to `dest`. `length` is the *compressed* size.
