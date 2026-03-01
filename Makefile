CXX := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra

OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4)
OPENCV_LIBS   := $(shell pkg-config --libs opencv4)

TAPPAS_FLAGS := $(shell pkg-config --cflags hailo-tappas-core)
# TAPPAS_LIBS  := -L/usr/lib/aarch64-linux-gnu/hailo/tappas/post_processes \
#                 -lyolov8pose_post \
#                 -lgsthailometa

TAPPAS_LIBS := -L/usr/lib/aarch64-linux-gnu/hailo/tappas/post_processes \
               -lyolov8pose_post \
               -lgsthailometa \
               -Wl,-rpath,/usr/lib/aarch64-linux-gnu/hailo/tappas/post_processes

CXXFLAGS += $(TAPPAS_FLAGS)

SRC := nn_video.cpp
BIN := nn_video

all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) $< -o $@ $(OPENCV_LIBS) -lhailort $(TAPPAS_LIBS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) *.o

.PHONY: all run clean