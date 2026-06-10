# Makefile for dvf (Vamped DNF)

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -pthread
CXXFLAGS = -Wall -Wextra -std=c++17 -D_GNU_SOURCE -g -MMD -MP -pthread

LDFLAGS = -pthread
LIBS =

# C++ FFI support configuration
CPP_LDFLAGS = -lcurl -lssl -lcrypto -lz -lpthread

# Source files
C_SOURCES = dvf-main.c dvf-config.c dvf-util.c dvf-rpm.c dvf-sqlite.c dvf-completion.c
HEADERS = dvf-config.h dvf-util.h dvf-rpm.h dvf-sqlite.h dvf-completion.h

TARGET = dvf

# --- Installation Variables ---
DESTDIR ?=
PREFIX ?= /usr
ETCDIR ?= /etc
REPO_DIR ?= $(ETCDIR)/yum.repos.d

.PHONY: all clean dvf debug install uninstall

.DEFAULT_GOAL := dvf

# Basic build (Core mode)
dvf: $(C_SOURCES:.c=.o)
	$(CC) $(C_SOURCES:.c=.o) -o $@ $(LDFLAGS) $(LIBS)

# FFI build (Advanced mode)
# Note: We force re-compilation of C objects with the FFI flag if 'all' is chosen
all: CFLAGS += -DENABLE_CPP_FFI
all: $(C_SOURCES:.c=.o) dvf-repo.o
	$(CXX) $(C_SOURCES:.c=.o) dvf-repo.o -o $(TARGET) $(LDFLAGS) $(CPP_LDFLAGS) $(LIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp dvf-repo.h $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up build artifacts..."
	rm -f *.o *.d $(TARGET)
	rm -rf test_rpms
	@echo "🧹 Clean complete."

install: all
	@echo "Installing $(TARGET) for Linux system to $(DESTDIR)$(PREFIX)/bin..."
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Determining configuration installation location..."
	@if [ -f dvf-config ]; then \
		echo "Installing system-wide configuration to $(DESTDIR)$(REPO_DIR)..."; \
		mkdir -p $(DESTDIR)$(REPO_DIR); \
		cp dvf-config $(DESTDIR)$(REPO_DIR)/dvf-config; \
		chmod 644 $(DESTDIR)$(REPO_DIR)/dvf-config; \
		echo "Configuration file installed to $(REPO_DIR)/dvf-config"; \
	fi
	@echo "-----------------------------------------------------------------------"
	@echo "⚡ DVF (Vamped DNF) successfully installed! ⚡"
	@echo ""
	@echo "Binary: $(PREFIX)/bin/$(TARGET)"
	@echo "Config: $(REPO_DIR)/dvf-config"
	@echo "-----------------------------------------------------------------------"

uninstall:
	@echo "Attempting to uninstall $(TARGET)..."
	@if [ -f $(DESTDIR)$(REPO_DIR)/dvf-config ]; then \
		echo "Removing configuration from $(DESTDIR)$(REPO_DIR)/dvf-config"; \
	fi
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(REPO_DIR)/dvf-config
	@echo "Uninstallation complete."

debug: CFLAGS += -DDEBUG -O0
debug: CXXFLAGS += -DDEBUG -O0
debug: all

-include *.d
