# Makefile for dvf (Vamped Up DNF Dandified YUM)

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -pthread
CXXFLAGS = -Wall -Wextra -std=c++17 -D_GNU_SOURCE -g -MMD -MP -pthread

LDFLAGS = -pthread
LIBS =

# C++ FFI support configuration
CPP_LDFLAGS = -lcurl -lssl -lcrypto -lz -lpthread

# Source files
C_SOURCES = dvf-main.c dvf-config.c dvf-util.c dvf-rpm.c dvf-sqlite.c dvf-completion.c dvf-storage.c dvf-hash.c
HEADERS = dvf-config.h dvf-util.h dvf-rpm.h dvf-sqlite.h dvf-completion.h dvf-storage.h dvf-hash.h

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
	@echo "⚡ DVF (Vamped Up DNF Dandified YUM) successfully installed! ⚡"
	@echo ""
	@echo "Binary: $(PREFIX)/bin/$(TARGET)"
	@echo "Config: $(REPO_DIR)/dvf-config"
	@echo "-----------------------------------------------------------------------"
	@echo "⚡ LIGHTNING FAST AUTOCOMPLETE ⚡"
	@echo "To enable context-aware completion for the current session, run:"
	@echo "  complete -C dvf dvf"
	@echo ""
	@echo "To enable it permanently, add that same line to your ~/.bashrc"
	@echo "-----------------------------------------------------------------------"

uninstall:
	@echo "Attempting to uninstall $(TARGET)..."
	@CONF=""; \
	if [ -f $(DESTDIR)$(REPO_DIR)/dvf-config ]; then \
		CONF="$(DESTDIR)$(REPO_DIR)/dvf-config"; \
	fi; \
	if [ -n "$$CONF" ]; then \
		echo "-----------------------------------------------------------------------"; \
		echo "NOTICE: Configuration found at $$CONF"; \
		echo "The following data directories may still exist and were NOT removed:"; \
		BASE=$$(grep "^dvf_dir[[:space:]]*=" $$CONF | cut -d= -f2 | tr -d ' \r\t'); \
		if [ -n "$$BASE" ]; then \
			echo "  - $$BASE (Base data)"; \
			echo "  - $$BASE/db (Package database)"; \
			echo "  - $$BASE/cache (Metadata/RPM cache)"; \
		fi; \
		grep -E "^(install_root|repo_dir)[[:space:]]*=" $$CONF | while read -r line; do \
			val=$$(echo "$$line" | cut -d= -f2 | tr -d '\r' | sed 's/^[[:space:]]*//;s/[[:space:]]*$$//'); \
			key=$$(echo "$$line" | cut -d= -f1 | tr -d ' \r\t'); \
			if [ "$$key" = "install_root" ]; then label="Install root"; else label="Repo directory"; fi; \
			echo "  - $$val ($$label)"; \
		done; \
		echo ""; \
		echo "You may want to remove these manually to fully purge $(TARGET) data."; \
		echo "-----------------------------------------------------------------------"; \
		echo "Removing configuration: $$CONF"; \
		rm -f $$CONF; \
	fi
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Uninstallation complete."

debug: CFLAGS += -DDEBUG -O0
debug: CXXFLAGS += -DDEBUG -O0
debug: all

-include *.d
