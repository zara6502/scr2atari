CXX = g++

CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -Wpedantic

TARGET = gtc

SRC = \
	src/main.cpp \
	src/gtc_dictionary.cpp \
	src/gtc_huffman.cpp \
	src/gtc_encoder.cpp \
	src/gtc_decoder.cpp

OBJ = $(SRC:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

rebuild: clean all

.PHONY: all clean rebuild