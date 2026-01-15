# ==========================================
#   Project Makefile
#   Compiler: clang
# ==========================================

# --- Terminal Colors & Formatting ---
# ANSI Escape Codes per colori brillanti e grassetto
# \033[1;32m = Verde Grassetto (Build)
# \033[1;33m = Giallo Grassetto (Clean)
# \033[1;36m = Ciano Grassetto (Exec)
# \033[0m    = Reset (Torna al colore default)

C_GREEN  = \033[1;32m
C_YELLOW = \033[1;33m
C_CYAN   = \033[1;36m
C_RESET  = \033[0m

# Etichette formattate con lunghezza fissa (Padding manuale)
# [BUILD] - 5 lettere
# [CLEAN] - 5 lettere
# [EXEC ] - 4 lettere + 1 spazio
TAG_BUILD = $(C_GREEN)[BUILD]$(C_RESET)
TAG_CLEAN = $(C_YELLOW)[CLEAN]$(C_RESET)
TAG_EXEC  = $(C_CYAN)[EXEC ]$(C_RESET)

# Compiler settings
CC = clang

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Output Executable
EXECUTABLE = $(BINDIR)/a.out

# Auto-detect source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

# --- Flags Configuration ---

# 1. Base Flags
BASE_FLAGS = -Wall -Wextra -Wpedantic -Wconversion -I$(SRCDIR)

# 2. Debug Flags
DEBUG_FLAGS = -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer

# 3. Production Flags
PROD_FLAGS = -O3 -march=native -flto -DNDEBUG

# Initialize CFLAGS with Debug settings by default
CFLAGS = $(BASE_FLAGS) $(DEBUG_FLAGS)

# --- Targets ---

.PHONY: all clean rebuild production run prod-run debug-run

# Default target: Builds the Debug version
all: $(EXECUTABLE)
	@echo "$(TAG_BUILD) Debug version compiled successfully."

# Production target: Builds the Release version
production: CFLAGS = $(BASE_FLAGS) $(PROD_FLAGS)
production: clean $(EXECUTABLE)
	@echo "$(TAG_BUILD) Production release compiled successfully."

# Link object files to create the executable
$(EXECUTABLE): $(OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_BUILD) Linking objects..."
	@$(CC) $(CFLAGS) $(OBJECTS) -o $(EXECUTABLE)

# Compile source files into object files
# Aggiunto echo qui per vedere il progresso per ogni file
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	@echo "$(TAG_BUILD) Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@rm -rf $(OBJDIR) $(BINDIR)
	@rm -rf *.dSYM
	@echo "$(TAG_CLEAN) Build artifacts removed."

# Rebuild the project (Debug mode)
rebuild: clean all

# Run the application
run: $(EXECUTABLE)
	@echo "$(TAG_EXEC) OUTPUT:\n"
	@./$(EXECUTABLE)
	@echo "\n$(TAG_EXEC) Finished."


# --- Convenience Targets ---

prod-run: production run

debug-run: rebuild run
