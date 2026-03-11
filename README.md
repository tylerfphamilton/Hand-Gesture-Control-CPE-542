# Dance DJ Deck

Control audio effects with body gestures using:
- **YOLOv8 Pose on Hailo (HailoRT)** for realtime shoulder/elbow/wrist tracking
- **PortAudio + custom DSP** for volume, bass, treble, and reverb control

This project runs as **one C++ application**:
1. Captures camera frames (USB webcam or Raspberry Pi Camera via GStreamer/libcamera)
2. Runs pose inference on a Hailo accelerator
3. Detects gestures based on arm joint geometry + hold times
4. Calls `AudioPlayer` effect setters in realtime (thread-safe atomics)

This repo can be reproduced by anyone with the same hardware stack.

---

## Demo Gestures

Using left/right arm keypoints:
- Left: shoulder(5), elbow(7), wrist(9)
- Right: shoulder(6), elbow(8), wrist(10)
Gestures implemented:

### Volume
- **Both arms held UP** for a set time will increment the volume in steps
- **Both arms held DOWN** for a set time will decrement the volume in steps

### Treble 
- If (with both arms) shoulder and elbow approximately same height and elbow angle is at 90° with the wrist **above** elbow
→ Enables/adjusts high-pass filter (cuts bass)

### Bass
- If (with both arms) shoulder and elbow approximately same height and elbow angle is at 90° with the wrist **below** elbow
→ Enables/adjusts low-pass filter (cuts treble)

### Reverb 
- **T-pose**: shoulder/elbow/wrist approximately same height on both arms
→ toggles reverb on/off

---

## Hardware Requirements

- **Raspberry Pi 5**
- **Hailo AI HAT** (Hailo-8)
- **Raspberry Pi AI Camera** (IMX500) *or* a USB webcam
- Any device PortAudio can output to (Pi headphone, HDMI, USB sound card)

---

## Software Requirements

- Raspberry Pi OS
- Packages:
  - OpenCV 
  - HailoRT runtime
  - Hailo TAPPAS core + YOLOv8 pose postprocess libs
  - Hailo precompiled models (`hailo-models`)
  - GStreamer + libcamera plugin (for RPi AI Camera mode)
  - Port Audio for audio output
  - Libsndfile for audio processing

---
## Physical Assembly

If using a camera connected via the CSI port (like the raspberry pi AI camera), plug it in now
Install the Hailo AI Hat by following the instructions here 
If using a camera connected via USB port, plug in it now
Connect peripheral devices (monitor, keyboard, mouse, and/or a speaker)
Connect the board to power

---
## Install Dependencies

```bash
sudo apt update
sudo apt install -y \
  install hailo-all \
  hailort hailo-tappas-core python3-hailo-tappas \
  sudo apt install -y hailo-models \
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
## How to Run

### Build

Copy this repository
Run
```bash
make clean && make
./nn_video <path_to_hef> <camera_mode> <input_audio_file>
```
### <path_to_hef>

The HEFs (Hailo Executable File) get uploaded via hailo-modes install, and give you several options to use. We suggest using 
/usr/share/hailo-models/yolov8s_pose_h8l_pi.hef
/usr/share/hailo-models/yolov8s_pose_h8.hef

### <camera_mode>

The camera mode is specified with a 0 or 1
0: USB connected camera
1: CSI connected camera

### <input_audio_file>

Provided in the repository are several test audio files you can use
Can also upload your own, must be in the .wav or .mp3 format

### Running

When running, a screen will appear with the live video feed that the camera is capturing
In the screen is a bounding box and the tracked arm movements
In the top left corner is the position the program is recognizing
PUT IMAGE HERE EXAMPLE
Printing to your terminal is the audio changes
IMAGE HERE EXAMPLE

### Notes

Must hit poses like specified in the images to get the desired audio change, as if arm angles are not exact enough, it will typically get recognized as volume up/down
With the reverb/T-pose, it is best to back up to a distance that your wrists will be in frame
Best to not have others in the background as the camera may pick them up and use include their movements
---
## Development Process

### Gesture Tracker

### Audio Output


### Linking the Two
