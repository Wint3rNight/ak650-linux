# AK650

Linux tool for the Ajazz AK650 keyboard. It puts your own image or GIF on the 128×128 screen and sets the keyboard's clock. Ajazz only ships a Windows driver for that.

Tested on a wired AK650 (bcdDevice 1.05): GIF upload and clock sync both work. Nobody had reverse-engineered this board, but its vendor HID descriptors are byte-identical to the AK820 Pro's, and the same upload sequence works.

## Build

Needs a C++20 compiler and CMake, nothing else. The image decoding and resizing code is vendored in `third_party/`.

```
cmake -B build
cmake --build build
cmake --install build --prefix ~/.local
```

That puts `ak650` in `~/.local/bin`. `ctest --test-dir build` runs the tests, which don't need the keyboard.

## Setup

The screen only works over the USB cable. The 2.4G dongle and Bluetooth don't expose its interfaces.

The hidraw nodes are root-only by default. Install the udev rule once so your login session gets access:

```
sudo install -m644 70-ajazz-ak650.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw
```

Then link the clock service (see below):

```
systemctl --user link "$PWD/ak650-time.service"
```

## Usage

```
ak650 probe                       # find the keyboard and check access, sends nothing
ak650 screen preview cat.gif      # writes cat-ak650.gif showing what would be sent
ak650 screen upload cat.gif
ak650 time                        # set the keyboard clock to system time
```

Takes GIF, PNG, JPG, BMP and TGA. `-v` prints every packet.

Images get cropped to a square by default. `--fit pad` letterboxes instead and `--fit stretch` stretches. GIFs over 30 frames are thinned out evenly with the loop length kept, because nobody has measured how much the flash holds; `--max-frames` goes up to 255 if you want to find out. Delays are stored in 2 ms steps, so anything outside 2–510 ms gets clamped.

Every upload rewrites the stored animation in flash, so don't put it on a timer. The image stays after a full power-off. Only one command talks to the keyboard at a time; a second one waits for the first.

`test-pattern.gif` is for checking a board or firmware I haven't tried: red, green, blue and white quadrants with a yellow border, and a counter from 1 to 8 at 250 ms a frame, so swapped colours, a shifted image or wrong timing are obvious. One loop should take 2 seconds. If the image only shows for a moment before the stock animation comes back, try `--slot 3`. If the upload succeeds and you still see the clock, the knob switches screen pages.

## Clock

The keyboard keeps its own clock, and on Windows the Ajazz driver sets it. On Linux nothing does, so it shows whatever it had. `ak650 time` sets it once. The second line of the udev rule starts `ak650-time.service` in your user session every time the board is plugged in over USB, so it gets fixed on each plug-in without you doing anything. Check that with `journalctl --user -u ak650-time`.

## F keys

The AK650 has no F row; Fn+1 to Fn+= send F1 to F12. On Linux they come out as brightness and volume keys instead. The board uses an Apple USB ID, so the kernel's `hid-apple` driver treats it as a Mac keyboard with media keys first. To get plain F keys:

```
echo "options hid_apple fnmode=2" | sudo tee /etc/modprobe.d/hid_apple.conf
echo 2 | sudo tee /sys/module/hid_apple/parameters/fnmode
```

The first line is for every boot, the second applies it now.

## How it works

The board enumerates as `05ac:024f`, Apple's Aluminium Keyboard ID, with `SONiX` / `AK650` as its USB strings. That's why the udev rule matches on the strings. It has two vendor interfaces: 0xFF13 takes 64-byte feature reports for commands, 0xFF68 takes 4096-byte output reports for pixel data.

An upload is START (`04 18`), image config (`04 72 <slot>`, chunk count at bytes 8–9), the payload in 4 KiB chunks with an input-report ack after each one, then COMMIT (`04 02`). The payload is a 256-byte header (frame count, then one delay byte per frame) followed by 128×128 RGB565 little-endian frames, padded with 0xFF. Every command waits 35 ms first; the board drops commands that come faster.

One Linux gotcha: on devices without report IDs, hidraw treats a leading 0x00 as the report ID and strips it. So every chunk goes out with an extra 0x00 in front, otherwise any chunk starting with a black pixel arrives a byte short.

The protocol comes from [wsclx/ak820pro-modder](https://github.com/wsclx/ak820pro-modder) (`docs/MACOS-800A.md`, the FF13 "family B" profile) and [aar-rafi/aks075-linux](https://github.com/aar-rafi/aks075-linux), which tracked down the hidraw issue.

Image loading and resizing use [stb](https://github.com/nothings/stb), and the preview GIF is written with [msf_gif](https://github.com/notnullnotvoid/msf_gif).
