#======================================================================
#  Makefile for the video_metric project
#======================================================================
#
#  Build the executable, check that gcc and ffmpeg are available,
#  and provide a clean target.
#
#  Usage:
#     make            # build video_metric
#     make clean      # remove build artefacts
#
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
	$(CC) $(LDFLAGS) -o $@ $^

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
	@echo "  all     – build video_metric"
	@echo "  clean   – remove build artefacts"
	@echo "  help    – this message"

#======================================================================
# End of Makefile
#======================================================================

