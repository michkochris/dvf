# Makefile for dvf (Vamped DNF)
# Based on the runepkg model

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -g -MMD -MP -pthread
CXXFLAGS = -Wall -Wextra -std=c++17 -D_GNU_SOURCE -g -MMD -MP -pthread

LDFLAGS = -pthread
LIBS =

# C++ FFI support detection
WITH_CPP ?= auto
CPP_LDFLAGS = -lcurl -lssl -lcrypto -lz -lpthread
CXX_AVAILABLE := $(shell command -v g++ >/dev/null 2>&1 && echo 1 || echo 0)
LIBCURL_AVAILABLE := $(shell pkg-config --exists libcurl 2>/dev/null && echo 1 || echo 0)
CPP_DEPS_AVAILABLE := $(shell command -v g++ >/dev/null 2>&1 && pkg-config --exists libcurl 2>/dev/null && echo 1 || echo 0)
CPP_WRAPPER_EXISTS := $(if $(wildcard dvf-repo.cpp),1,0)
CPP_FFI_AVAILABLE := $(if $(and $(filter 1,$(CPP_DEPS_AVAILABLE)),$(filter 1,$(CPP_WRAPPER_EXISTS))),1,0)

ifeq ($(WITH_CPP),auto)
    ifeq ($(CPP_FFI_AVAILABLE),1)
        WITH_CPP = 1
    else
        WITH_CPP = 0
    endif
endif

ifeq ($(WITH_CPP),1)
    CFLAGS += -DENABLE_CPP_FFI
    LDFLAGS += $(CPP_LDFLAGS)
    CPP_SOURCES = dvf-repo.cpp
    CPP_OBJECTS = $(CPP_SOURCES:.cpp=.o)
    CPP_HEADERS = dvf-repo.h
else
    CPP_SOURCES =
    CPP_OBJECTS =
    CPP_HEADERS =
endif

TARGET = dvf

# --- Installation Variables ---
DESTDIR ?=
PREFIX ?= /usr
ETCDIR ?= /etc
REPO_DIR ?= $(ETCDIR)/yum.repos.d

# Source files
C_SOURCES = dvf-main.c dvf-config.c dvf-util.c dvf-rpm.c dvf-sqlite.c dvf-completion.c
OBJS = $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)

# Header files
HEADERS = dvf-config.h dvf-util.h dvf-rpm.h dvf-sqlite.h dvf-completion.h $(CPP_HEADERS)

.PHONY: all clean dvf debug install uninstall

.DEFAULT_GOAL := dvf

dvf: WITH_CPP=0
dvf: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)

all: WITH_CPP=1
all: $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up build artifacts..."
	rm -f $(OBJS) $(TARGET) $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d)
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
debug: dvf

-include $(C_SOURCES:.c=.d) $(CPP_SOURCES:.cpp=.d)
