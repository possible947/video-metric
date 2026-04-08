#======================================================================
#  Makefile for the video_metric project
#======================================================================

# Directories and file names
SRC_DIR   := src
OBJ_DIR   := obj
BIN       := video_metric

# Compiler and flags
CC        := gcc
CFLAGS    := -std=gnu11 -Wall -Wextra -O2 -I. -I$(SRC_DIR) \
             -D_GNU_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong
LDFLAGS   := -lm

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
SRCS      := $(wildcard $(SRC_DIR)/*.c)
OBJS      := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean windows-build help

# Default target
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

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
	rm -f $(OBJS) $(BIN)
	rmdir /S /Q $(OBJ_DIR) 2>nul || rm -rf $(OBJ_DIR)
	rm -f vmaf_*.json
	@echo "Done."

help:
	@echo "Makefile targets:"
	@echo "  all                     – build video_metric (auto‑detect threading)"
	@echo "  windows-build           – build for Windows using MinGW"
	@echo "  MSSSIM_USE_OPENMP=yes   – force OpenMP (requires -fopenmp support)"
	@echo "  MSSSIM_USE_OPENMP=no    – force pthreads (macOS system clang)"
	@echo "  MSSSIM_SINGLE_THREAD=yes – disable all threading"
	@echo "  clean                   – remove build artefacts"
	@echo "  help                    – this message"

# End of Makefile
