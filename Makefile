# ---- Settings ----
CXX := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra
LDFLAGS = `pkg-config --libs opencv4`
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4)
OPENCV_LIBS := $(shell pkg-config --libs opencv4)

SRC := nn_video.cpp
BIN := nn_video

# ---- Targets ----
all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) $< -o $@ $(OPENCV_LIBS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) *.o

.PHONY: all run clean
