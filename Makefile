#======================================================================
#  Makefile for the video_metric project
#======================================================================

# Directories and file names
SRC_DIR   := src
OBJ_DIR   := obj
BIN       := video_metric
GUI_BIN   := video_metric_gui

# --- Compiler selection ---
# On macOS: if CC was not passed on the command line, try MacPorts clang
# (versions 18 → 20 → 16 → 14) which supports OpenMP.  Fall back to the
# system default compiler (Apple clang / cc) with pthreads when not found.
# On Linux (or when CC is set explicitly): use whatever CC says.
ifeq ($(origin CC),default)
ifeq ($(shell uname -s 2>/dev/null),Darwin)
    _MP_CLANG := $(firstword $(foreach v,18 20 16 14,\
        $(if $(shell command -v clang-mp-$(v) 2>/dev/null),clang-mp-$(v),)))
    ifneq ($(_MP_CLANG),)
        CC := $(_MP_CLANG)
        $(info [compiler] MacPorts $(_MP_CLANG) auto-selected)
        ifeq ($(MSSSIM_USE_OPENMP),)
            MSSSIM_USE_OPENMP := yes
        endif
    else
        $(info [compiler] MacPorts clang not found, using system cc + pthreads)
    endif
endif
endif

# Compiler flags
CFLAGS    := -std=gnu11 -Wall -Wextra -O2 -I. -I$(SRC_DIR) \
             -D_GNU_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong
LDFLAGS   := -lm

# GTK4 flags (used only for GUI target)
GTK_CFLAGS := $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs   gtk4 2>/dev/null)

# Threading configuration
ifeq ($(MSSSIM_SINGLE_THREAD),yes)
    CFLAGS += -DMSSSIM_SINGLE_THREAD
    $(info [threading] single-threaded mode)

else ifeq ($(MSSSIM_USE_OPENMP),yes)
    CFLAGS  += -fopenmp
    LDFLAGS += -fopenmp
    $(info [threading] OpenMP enabled (explicit))

else ifeq ($(MSSSIM_USE_OPENMP),no)
    CFLAGS  += -pthread
    LDFLAGS += -pthread
    $(info [threading] pthreads enabled (explicit))

else
    _OMP_TEST := $(shell printf 'int main(){return 0;}' | \
                     $(CC) -fopenmp -x c - -o /dev/null 2>/dev/null && echo yes)
    ifeq ($(_OMP_TEST),yes)
        CFLAGS  += -fopenmp
        LDFLAGS += -fopenmp
        $(info [threading] OpenMP auto-detected and enabled)
    else
        CFLAGS  += -pthread
        LDFLAGS += -pthread
        $(info [threading] OpenMP not available, using pthreads)
    endif
endif

# Windows specific flags (MinGW)
ifeq ($(OS),Windows_NT)
    CFLAGS += -D_WIN32
    LDFLAGS += -lshlwapi
endif

# Shared computational sources (used by both CLI and GUI)
COMPUTE_SRCS := \
	$(SRC_DIR)/env_check.c \
	$(SRC_DIR)/path_util.c \
	$(SRC_DIR)/ssim.c \
	$(SRC_DIR)/ms_ssim.c \
	$(SRC_DIR)/msssim_core.c \
	$(SRC_DIR)/pipe_decoder.c \
	$(SRC_DIR)/vmaf.c

# CLI-only sources
CLI_SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/cli_parser.c \
	$(COMPUTE_SRCS)

# GUI-only sources
GUI_SRCS := \
	$(SRC_DIR)/gui_main.c \
	$(SRC_DIR)/ui_main.c \
	$(SRC_DIR)/app_state.c \
	$(SRC_DIR)/worker.c \
	$(COMPUTE_SRCS)

CLI_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CLI_SRCS))
GUI_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/gui_%.o,$(GUI_SRCS))

.PHONY: all gui clean windows-build help

# Default target builds CLI only; 'make gui' builds the GUI
all: $(BIN)

gui: $(GUI_BIN)

$(BIN): $(CLI_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(GUI_BIN): $(GUI_OBJS)
	@if [ -z "$(GTK_LIBS)" ]; then \
		echo "Error: GTK4 not found. Install gtk4 via MacPorts: sudo port install gtk4 +quartz"; \
		exit 1; \
	fi
	$(CC) -o $@ $^ $(LDFLAGS) $(GTK_LIBS)

# CLI objects use standard flags
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# GUI objects compiled with GTK4 flags (separate obj/ prefix to avoid collisions)
$(OBJ_DIR)/gui_%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

# Windows build target (MinGW)
windows-build:
ifeq ($(OS),Windows_NT)
	@echo "Building for Windows (MinGW)..."
	make all
else
	@echo "This target is only available on Windows."
endif

clean:
	@echo "Removing build artefacts..."
	rm -f $(CLI_OBJS) $(GUI_OBJS) $(BIN) $(GUI_BIN)
ifeq ($(OS),Windows_NT)
	-rmdir /S /Q $(OBJ_DIR) 2>NUL
else
	rm -rf $(OBJ_DIR)
endif
	rm -f vmaf_*.json
	@echo "Done."

help:
	@echo "Makefile targets:"
	@echo "  all                      – build video_metric (CLI)"
	@echo "  gui                      – build video_metric_gui (GTK4, requires gtk4 pkg-config)"
	@echo "  windows-build            – build for Windows using MinGW"
	@echo "  MSSSIM_USE_OPENMP=yes    – force OpenMP (MacPorts clang: make CC=clang-mp-20 MSSSIM_USE_OPENMP=yes gui)"
	@echo "  MSSSIM_USE_OPENMP=no     – force pthreads (macOS system clang)"
	@echo "  MSSSIM_SINGLE_THREAD=yes – disable all threading"
	@echo "  clean                    – remove build artefacts"
	@echo "  help                     – this message"

# End of Makefile
