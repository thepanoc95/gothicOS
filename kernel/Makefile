ROOT := $(abspath ..)
BUILD := $(ROOT)/build/NativeKernel
OBJ := $(BUILD)/obj

CC ?= cc
CXX ?= g++
INCLUDES := -I$(ROOT)/kernel/core/include -I$(ROOT)/kernel/hal/cpp/include \
	-I$(ROOT)/kernel/kal/cpp/include -I$(ROOT)/kernel/drivers/cpp/include \
	-I$(ROOT)/kernel/linux/include -I$(ROOT)/SDK/include
CFLAGS := -std=c11 -Wall -Wextra -Werror -fPIC -O2 $(INCLUDES)
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -fPIC -O2 $(INCLUDES)

CORE_SRC := $(ROOT)/kernel/core/c/nk_core.c
HANDOFF_SRC := $(ROOT)/kernel/linux/handoff.c
CXX_SRC := $(ROOT)/kernel/hal/cpp/src/hal.cpp \
	$(ROOT)/kernel/kal/cpp/src/kal.cpp \
	$(ROOT)/kernel/drivers/cpp/src/drivers.cpp

.PHONY: all core cpp rust test clean
all: core cpp rust

$(OBJ):
	mkdir -p $(OBJ)

$(OBJ)/nk_core.o: $(CORE_SRC) $(ROOT)/kernel/core/include/gothicos/nk_core.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/nk_handoff.o: $(HANDOFF_SRC) $(ROOT)/kernel/linux/include/gothicos/nk_handoff.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/libnk_core.so: $(OBJ)/nk_core.o
	$(CC) -shared -o $@ $^

$(BUILD)/libnk_handoff.so: $(OBJ)/nk_handoff.o
	$(CC) -shared -o $@ $^

$(OBJ)/hal.o: $(ROOT)/kernel/hal/cpp/src/hal.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ)/kal.o: $(ROOT)/kernel/kal/cpp/src/kal.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ)/drivers.o: $(ROOT)/kernel/drivers/cpp/src/drivers.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/libgothicos_stack.so: $(OBJ)/hal.o $(OBJ)/kal.o $(OBJ)/drivers.o $(OBJ)/nk_core.o
	$(CXX) -shared -o $@ $^

core: $(BUILD)/libnk_core.so $(BUILD)/libnk_handoff.so
cpp: $(BUILD)/libgothicos_stack.so

rust:
	cargo build --workspace --offline --manifest-path $(ROOT)/kernel/Cargo.toml

test: all
	cargo test --workspace --offline --manifest-path $(ROOT)/kernel/Cargo.toml

clean:
	rm -rf $(BUILD) $(ROOT)/kernel/target
