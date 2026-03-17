# Dance DJ Deck

This project turns your body into a DJ controller. You can raise both arms to crank up the volume, hold out a T-pose to toggle reverb, and flex at 90 degrees to boost bass or treble. All in real time.
The system works by running a YOLOv8 pose estimation neural network on a Hailo AI accelerator attached to a Raspberry Pi 5. The pose model continuously tracks the positions of your shoulders, elbows, and wrists. A gesture recognition layer then interprets the geometry of your arms and maps those gestures to audio effects, which are applied live to a .wav file playing through PortAudio.
This guide will walk you through everything: the hardware assembly, software installation, how the gesture and audio systems work under the hood, and how those two systems are wired together into a single C++ application. By the end, you'll have a working system and a solid understanding of how it all fits together.

---

## Gestures

| Gesture | Arm Position | Effect |
|---|---|---|
| Both arms **UP** (held) | Wrists above shoulders | Volume + |
| Both arms **DOWN** (held) | Wrists below hips | Volume - |
| **90° Up** (both arms) | Shoulder & elbow level, wrists above elbows | Treble boost (high-pass) |
| **90° Down** (both arms) | Shoulder & elbow level, wrists below elbows | Bass boost (low-pass) |
| **T-Pose** | Shoulder, elbow, and wrist all at same height | Reverb toggle |

The system tracks six keypoints from YOLOv8's pose output: left/right shoulder (5, 6), elbow (7, 8), and wrist (9, 10). Gestures are confirmed after a configurable hold time to prevent accidental triggers.

---

## Hardware Requirements
Before installing any software, make sure you have all the necessary components on hand. This project relies on a specific hardware stack; substitutions may require changes to the build configuration.
Raspberry Pi 5 (4GB or 8GB recommended)
Hailo AI HAT (Hailo-8 accelerator)
Raspberry Pi AI Camera (IMX500) OR any USB webcam
MicroSD card (32GB+, with Raspberry Pi OS installed)
Monitor, keyboard, and mouse
Speaker or headphones (3.5mm jack, HDMI audio, or USB sound card)
Power supply for Raspberry Pi 5 (USB-C, 5V/5A)

> **Why Hailo?** Running YOLOv8 pose estimation on a CPU at real-time framerates (30fps) would overwhelm the Raspberry Pi 5's ARM cores. The Hailo-8 AI accelerator offloads the neural network inference entirely, leaving the CPU free to handle gesture logic and audio processing. Without it, you'd be limited to very low framerates, making gesture detection sluggish and unreliable.

---
## Physical Assembly
Assemble the hardware before you install any software. This ensures the OS can detect all peripherals on first boot.
1. Install the Hailo AI HAT onto the Raspberry Pi 5's GPIO header. The HAT connects via the PCIe M.2 slot on the Pi 5's underside. Follow the official Hailo assembly guide included with the HAT. It involves securing standoffs and connecting a short ribbon cable to the PCIe connector.
2. Connect your camera. If using the Raspberry Pi AI Camera, plug the CSI ribbon cable into the camera port on the Pi. If using a USB webcam, simply plug it into any USB port. You'll specify which type during runtime with a command-line flag.
3. Connect peripherals, a monitor (via HDMI), keyboard, and mouse.
4. Connect your audio output. Can use a USB audio device, HDMI to a monitor with speakers, or connect the Pi to a Bluetooth speaker.
5. Power on by connecting the USB-C power supply. Boot into Raspberry Pi OS.
---

## Install Software Dependencies

The project depends on several libraries. Here's what each one does and why you need it:

### Understanding the Dependencies
#### Hailo Runtime and TAPPAS
The HailoRT runtime is the low-level driver and API for communicating with the Hailo-8 chip over PCIe. TAPPAS (Toolkit for Application Performance, Analytics, and Systems) is Hailo's higher-level framework that provides pre-built GStreamer elements and post-processing plugins for common tasks like object detection and pose estimation. Without these, the Hailo chip is an inert piece of silicon.

#### YOLOv8 Pose Model (.hef)
A .hef file (Hailo Executable File) is a compiled neural network. It’s analogous to a .tflite or ONNX file, but pre-compiled specifically for the Hailo-8 architecture. The hailo-models package installs several pre-compiled models, including two YOLOv8 pose variants. You do not need to train or compile anything yourself.

#### OpenCV
OpenCV handles camera capture and video rendering. It provides the frame-by-frame camera read loop and draws the bounding boxes and skeleton overlay on the live display window. It's the 'eyes' that feed pixel data into the Hailo pipeline.

#### PortAudio + libsndfile
PortAudio is a cross-platform audio I/O library that handles real-time audio streaming to your output device. libsndfile handles reading .wav audio files from disk. Together, they form the audio playback engine. libsndfile decodes the file, and PortAudio streams the processed PCM samples to your speakers.

#### GStreamer
GStreamer is a media pipeline framework used when capturing from the Raspberry Pi AI Camera (CSI mode). It handles the camera's proprietary output format and delivers frames to the application. If you use a USB webcam, OpenCV handles capture directly and GStreamer is not involved in the capture path.

### Install Commands
Run the following in a terminal on your Raspberry Pi:

```bash
sudo apt update
sudo apt install -y \
  hailo-all \
  hailort hailo-tappas-core python3-hailo-tappas \
  hailo-models \
  build-essential cmake pkg-config git \
  libopencv-dev \
  libportaudio2 portaudio19-dev \
  libsndfile1 libsndfile1-dev \
  gstreamer1.0-tools \
  gstreamer1.0-libav \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly
```

---
### Build

```bash
make clean && make
```

The Makefile links against HailoRT, TAPPAS, OpenCV, PortAudio, and libsndfile. If the build fails, the linker error will name the missing library. Check the install step above.

### Run

```bash
./nn_video <path_to_hef> <camera_mode> <input_audio_file>
```

**`<path_to_hef>`**: Choose based on your HAT variant (check the PCB silkscreen or run `hailortcli fw-control identify`):

```
/usr/share/hailo-models/yolov8s_pose_h8l_pi.hef   # Hailo-8L
/usr/share/hailo-models/yolov8s_pose_h8.hef        # Hailo-8
```

**`<camera_mode>`**
- `0`: USB webcam (OpenCV direct capture)
- `1`: Raspberry Pi AI Camera via CSI (GStreamer + libcamera)

**`<input_audio_file>`**: Must be `.wav`. Test files are included in the repo. To convert your own:

```bash
ffmpeg -i your_song.mp3 output.wav
```

### Example

```bash
./nn_video /usr/share/hailo-models/yolov8s_pose_h8.hef 0 input.wav
```
---
## How It Works

### Architecture Overview

Three things happen concurrently when the app runs:

1. **Audio callback thread**: PortAudio opens a real-time stream and continuously fills the sound card buffer with DSP-processed samples. This thread runs for the lifetime of the app at audio sample rate.
2. **Main loop**: Captures frames from the camera, pre-processes them for the Hailo chip, runs YOLOv8 pose inference via HailoRT, and passes the resulting keypoints to the gesture tracker.
3. **Gesture tracker**: Evaluates arm geometry on each frame, updates hold-time counters, and calls a setter on `AudioPlayer` when a gesture is confirmed.

### Gesture Detection

For each frame, the tracker receives the six arm keypoint coordinates and applies geometric rules:

- **Volume**: Checks whether wrists are above shoulders (up) or below hips (down) on both sides simultaneously. A hold counter must reach a threshold before volume steps, preventing accidental triggers.
- **Treble / Bass**: First confirms shoulder and elbow are at roughly the same height (horizontal arms). Then computes the elbow angle from the shoulder→elbow and elbow→wrist vectors. ~90° with wrist above = treble; wrist below = bass.
- **Reverb (T-pose)**: Checks that shoulder, elbow, and wrist are all at approximately the same y-coordinate on both arms.

Angle tolerances and hold durations are configurable constants at the top of the gesture tracker source.

### Audio Engine

`AudioPlayer` wraps PortAudio and implements four DSP effects:

- **Volume**: Gain multiplier applied to every sample.
- **High-pass filter (treble)**: Biquad IIR filter that attenuates low frequencies.
- **Low-pass filter (bass)**: Biquad IIR filter that attenuates high frequencies.
- **Reverb**: Feedback delay network that mixes delayed, attenuated copies of the signal with the dry output.

### Connecting the Two

The gesture tracker holds a reference to `AudioPlayer` and calls one of four setters when a gesture fires:

```cpp
audioPlayer.setVolume(newVolume);     // float in [0.0, 1.0]
audioPlayer.enableHighPass(true);     // treble gesture
audioPlayer.enableLowPass(true);      // bass gesture
audioPlayer.toggleReverb();           // T-pose
```

All effect parameters are stored as `std::atomic` values so the gesture thread can write a new value at any moment and the audio callback reads it on the next buffer fill so that there are no locks, no blocking, no audio glitches.

### Video Overlay

The live window shows:
- Bounding box around the detected person
- Lines connecting arm keypoints (shoulder → elbow → wrist)
- Current recognized gesture label in the top-left corner

If you're holding a pose and nothing is happening, the overlay label will tell you what gesture the system is actually reading.

---

## Troubleshooting

**T-pose not triggering**: Step back until all three joints (shoulder, elbow, wrist) are fully in frame. The model can't track joints it can't see.

**Other people being detected**: The model picks the highest-confidence person in frame. Keep the background clear or use a camera angle that frames only you.

**90° poses triggering volume instead**: Check the overlay to see what the system is reading. Your elbow angle may not be close enough to 90°. You can also widen the angle tolerance constant in the gesture tracker source.

**No audio output**: Run `pactl list sinks` to list available output devices. Switch the default with `pactl set-default-sink <sink_name>`.

**Hailo not detected**: Re-seat the ribbon cable and verify with `hailortcli fw-control identify`. The PCIe connection is sensitive to partial insertion.

---
