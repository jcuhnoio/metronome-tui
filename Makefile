CXX := clang++
FTXUI_PREFIX := $(shell brew --prefix ftxui)
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I$(FTXUI_PREFIX)/include
LDFLAGS := -L$(FTXUI_PREFIX)/lib -lftxui-screen -lftxui-dom -lftxui-component
TARGET := metronome
SRCS := metronome.cpp
OBJS := $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
