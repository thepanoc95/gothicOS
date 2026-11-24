# LVGL host simulator: renders the desktop through the port layer.
#
#   make            render build/lvgl_sim/dashboard.png
#   make render     same, but keeps the PPM
#   make clean      drop simulator output (not the LVGL download)
#
# LVGL itself is fetched into build/third_party by ../fetch_lvgl.sh and is not
# part of the repository.

NULLK   := $(abspath ../..)
BUILD   := $(abspath $(NULLK)/../build)/lvgl_sim
PORT    := $(NULLK)/shared/lvgl_port
# The script downloads and verifies LVGL on first use, then prints its path.
LVGL    := $(shell $(NULLK)/tools/fetch_lvgl.sh)

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
           -DLV_CONF_INCLUDE_SIMPLE
CPPFLAGS += -I$(PORT) -I$(LVGL) -I$(NULLK)/include

LVGL_SRC := $(shell find $(LVGL)/src -name '*.c' 2>/dev/null)
LVGL_OBJ := $(patsubst $(LVGL)/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRC))
PORT_OBJ := $(BUILD)/port/lvgl_port.o $(BUILD)/port/lvgl_terminal.o \
            $(BUILD)/port/lvgl_dashboard.o $(BUILD)/port/main.o \
            $(BUILD)/port/interfacekit_gfx.o

BIN := $(BUILD)/lvgl_sim
PPM := $(BUILD)/dashboard.ppm
PNG := $(BUILD)/dashboard.png
PPM_TERM := $(BUILD)/terminal.ppm
PNG_TERM := $(BUILD)/terminal.png

.PHONY: all render run clean check

all: $(PNG) $(PNG_TERM)

$(BUILD)/lvgl/%.o: $(LVGL)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/port/lvgl_dashboard.o: $(PORT)/lvgl_dashboard.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/port/lvgl_terminal.o: $(PORT)/lvgl_terminal.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/port/lvgl_port.o: $(PORT)/lvgl_port.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/port/interfacekit_gfx.o: $(NULLK)/shared/interfacekit_gfx.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/port/main.o: main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BIN): $(LVGL_OBJ) $(PORT_OBJ)
	$(CC) -o $@ $^ -lm

$(PPM): $(BIN)
	$(BIN) $@ 1280 800 0

$(PPM_TERM): $(BIN)
	$(BIN) $@ 1280 800 1

$(PNG): $(PPM)
	python3 ppm2png.py $< $@
	@echo "wrote $@"

$(PNG_TERM): $(PPM_TERM)
	python3 ppm2png.py $< $@
	@echo "wrote $@"

render: $(PPM) $(PPM_TERM)

run: $(BIN)
	$(BIN) $(PPM) 1280 800 0
	$(BIN) $(PPM_TERM) 1280 800 1

# Compile-check the port layer as freestanding code, the way nullk builds it.
check:
	@mkdir -p $(BUILD)
	$(CC) -c -std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
		-nostdlib -m64 -mno-red-zone -fno-pic -fno-pie -O2 -Werror \
		-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
		-DLV_CONF_INCLUDE_SIMPLE $(CPPFLAGS) \
		-o $(BUILD)/lvgl_port_check.o $(PORT)/lvgl_port.c
	$(CC) -c -std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
		-nostdlib -m64 -mno-red-zone -fno-pic -fno-pie -O2 -Werror \
		-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
		-DLV_CONF_INCLUDE_SIMPLE $(CPPFLAGS) \
		-o $(BUILD)/lvgl_terminal_check.o $(PORT)/lvgl_terminal.c
	@echo "lvgl_port.c and lvgl_terminal.c compile freestanding"

clean:
	rm -rf $(BUILD)
