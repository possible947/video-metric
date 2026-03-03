#======================================================================
#  Makefile for the video_metric project
#======================================================================
#
#  Build the executable, check that gcc and ffmpeg are available,
#  and provide a clean target.
#
#  Usage:
#     make                          # build video_metric (auto-detect threading)
#     make MSSSIM_USE_OPENMP=yes    # force OpenMP (requires -fopenmp support)
#     make MSSSIM_USE_OPENMP=no     # force pthreads (even if OpenMP available)
#     make MSSSIM_SINGLE_THREAD=yes # disable all threading
#     make clean                    # remove build artefacts
#
#  macOS (system clang, no OpenMP):
#     make MSSSIM_USE_OPENMP=no
#
#  macOS (Homebrew llvm with OpenMP):
#     CC=/usr/local/opt/llvm/bin/clang make MSSSIM_USE_OPENMP=yes
#---------------------------------------------------------------------

#----------------------------------------------------------------------
# Directories and file names
#----------------------------------------------------------------------
SRC_DIR   := src
OBJ_DIR   := obj
BIN       := video_metric

#----------------------------------------------------------------------
# Compiler and flags
#----------------------------------------------------------------------
CC        := gcc
CFLAGS    := -std=gnu11 -Wall -Wextra -O2 -I$(SRC_DIR) \
             -D_GNU_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong
LDFLAGS   := -lm

#----------------------------------------------------------------------
# Threading configuration
#----------------------------------------------------------------------
# Disable multithreading entirely
ifeq ($(MSSSIM_SINGLE_THREAD),yes)
    CFLAGS += -DMSSSIM_SINGLE_THREAD
    $(info [threading] single-threaded mode)

else ifeq ($(MSSSIM_USE_OPENMP),yes)
    # OpenMP explicitly requested
    CFLAGS  += -fopenmp
    LDFLAGS += -fopenmp
    $(info [threading] OpenMP enabled (explicit))

else ifeq ($(MSSSIM_USE_OPENMP),no)
    # pthreads explicitly requested (e.g. macOS system clang)
    CFLAGS  += -pthread
    LDFLAGS += -pthread
    $(info [threading] pthreads enabled (explicit))

else
    # Auto-detect: prefer OpenMP, fall back to pthreads
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

#----------------------------------------------------------------------
# Source files and corresponding object files
#----------------------------------------------------------------------
SRCS      := $(wildcard $(SRC_DIR)/*.c)
OBJS      := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

#----------------------------------------------------------------------
# Check that required tools exist
#----------------------------------------------------------------------
# If gcc is not found, abort with a user‑friendly message
ifeq ($(shell command -v $(CC) 2>/dev/null),)
$(error "gcc not found – install the GNU compiler collection")
endif

# If ffmpeg is not found, abort with a user‑friendly message
ifeq ($(shell command -v ffmpeg 2>/dev/null),)
$(error "ffmpeg not found – install FFmpeg")
endif

#----------------------------------------------------------------------
# Default target
#----------------------------------------------------------------------
.PHONY: all
all: $(OBJ_DIR) $(BIN)

#----------------------------------------------------------------------
# Build the executable
#----------------------------------------------------------------------
$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

#----------------------------------------------------------------------
# Compile each source file into the object directory
#----------------------------------------------------------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

#----------------------------------------------------------------------
# Ensure the object directory exists
#----------------------------------------------------------------------
$(OBJ_DIR):
	@mkdir -p $@

#----------------------------------------------------------------------
# Clean build artefacts
#----------------------------------------------------------------------
.PHONY: clean
clean:
	@echo "Removing build artefacts..."
	@rm -f $(OBJS) $(BIN)
	@rm -rf $(OBJ_DIR)
	@# Delete any temporary VMAF JSON logs that might have been left
	@rm -f vmaf_*.json
	@echo "Done."

#----------------------------------------------------------------------
# Help target (optional)
#----------------------------------------------------------------------
.PHONY: help
help:
	@echo "Makefile targets:"
	@echo "  all                       – build video_metric (auto-detect threading)"
	@echo "  MSSSIM_USE_OPENMP=yes     – build with OpenMP"
	@echo "  MSSSIM_USE_OPENMP=no      – build with pthreads (macOS system clang)"
	@echo "  MSSSIM_SINGLE_THREAD=yes  – build single-threaded"
	@echo "  clean                     – remove build artefacts"
	@echo "  help                      – this message"
	@echo ""
	@echo "Runtime tuning:"
	@echo "  MSSSIM_THREADS=<n>  – set number of worker threads via env var"

#======================================================================
# End of Makefile
#======================================================================

