TARGET = build/ETDSelect.exe
CXX = $(MINGW_PREFIX)g++
WINDRES = $(MINGW_PREFIX)windres

CXXFLAGS = -std=c++17 -O2 -DUNICODE -D_UNICODE -mwindows
LDFLAGS = -lgdiplus -lcomctl32 -lmsimg32 -lcomdlg32 -lole32 -lshlwapi -luuid

SRCS = src/main.cpp src/app.cpp src/settings.cpp src/overlay.cpp src/editor.cpp
OBJS = $(patsubst src/%.cpp,build/%.o,$(SRCS))
RES_OBJ = build/resource.o

all: build_dir $(TARGET)

build_dir:
	@mkdir -p build

$(TARGET): $(OBJS) $(RES_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RES_OBJ): src/resource.rc src/app.manifest
	$(WINDRES) $< -o $@

clean:
	@rm -rf build

install: $(TARGET)
	@mkdir -p "$(PROGRAMFILES)/ETDSelect"
	@cp $(TARGET) "$(PROGRAMFILES)/ETDSelect/"

.PHONY: all build_dir clean install
