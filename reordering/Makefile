CC       := gcc
CFLAGS   := -O2 -Wall -Wextra
LIBDLB   := $(CURDIR)/dlb/libdlb
CPPFLAGS := -I$(LIBDLB)
LDFLAGS  := -L$(LIBDLB) -Wl,-rpath,$(LIBDLB)
LDLIBS   := -ldlb -lpthread

SRC_DIR  := src
BIN_DIR  := bin
SRCS     := $(wildcard $(SRC_DIR)/*.c)
BINS     := $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(SRCS))

.PHONY: all clean libdlb

all: $(BINS)

$(BIN_DIR)/%: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Rebuild Intel's libdlb if ever needed
libdlb:
	$(MAKE) -C $(LIBDLB)

clean:
	rm -rf $(BIN_DIR)