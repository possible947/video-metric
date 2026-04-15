#======================================================================
#  Makefile for the video_metric project
#======================================================================

# Directories and file names
SRC_DIR   := src
OBJ_DIR   := obj
BIN       := video_metric
GUI_BIN   := video_metric_gui

# Compiler and flags
CC        := gcc
CFLAGS    := -std=gnu11 -Wall -Wextra -O2 -I. -I$(SRC_DIR) \
             -D_GNU_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong
LDFLAGS   := -lm
GTK_CFLAGS := $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk4 2>/dev/null)

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

# Source files and corresponding object files
CLI_SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/cli_parser.c \
	$(SRC_DIR)/env_check.c \
	$(SRC_DIR)/path_util.c \
	$(SRC_DIR)/ssim.c \
	$(SRC_DIR)/ms_ssim.c \
	$(SRC_DIR)/msssim_core.c \
	$(SRC_DIR)/pipe_decoder.c \
	$(SRC_DIR)/vmaf.c

GUI_SRCS := \
	$(SRC_DIR)/gui_main.c \
	$(SRC_DIR)/ui_main.c \
	$(SRC_DIR)/app_state.c \
	$(SRC_DIR)/runner.c \
	$(SRC_DIR)/output_parser.c \
	$(SRC_DIR)/validation.c

CLI_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CLI_SRCS))
GUI_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(GUI_SRCS))

.PHONY: all clean windows-build help

# Default target
all: $(BIN) $(GUI_BIN)

$(BIN): $(CLI_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(GUI_BIN): $(GUI_OBJS)
	@if [ -z "$(GTK_LIBS)" ]; then \
		echo "Error: GTK4 not found via pkg-config. Install gtk4 development files."; \
		exit 1; \
	fi
	$(CC) -o $@ $^ $(LDFLAGS) $(GTK_LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
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
	rmdir /S /Q $(OBJ_DIR) 2>nul || rm -rf $(OBJ_DIR)
	rm -f vmaf_*.json
	@echo "Done."

help:
	@echo "Makefile targets:"
	@echo "  all                     – build video_metric and video_metric_gui"
	@echo "  windows-build           – build for Windows using MinGW"
	@echo "  MSSSIM_USE_OPENMP=yes   – force OpenMP (requires -fopenmp support)"
	@echo "  MSSSIM_USE_OPENMP=no    – force pthreads (macOS system clang)"
	@echo "  MSSSIM_SINGLE_THREAD=yes – disable all threading"
	@echo "  video_metric_gui        – build GTK4 GUI wrapper (requires gtk4 pkg-config)"
	@echo "  clean                   – remove build artefacts"
	@echo "  help                    – this message"

# End of Makefile
