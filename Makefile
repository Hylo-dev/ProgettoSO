# ==========================================
#   Project Makefile (Modular Architecture)
#   Compiler: clang
# ==========================================

# --- Terminal Colors & Formatting ---
C_GREEN  = \033[1;32m
C_YELLOW = \033[1;33m
C_CYAN   = \033[1;36m
C_RED    = \033[1;31m
C_RESET  = \033[0m

TAG_BUILD = $(C_GREEN)[BUILD]$(C_RESET)
TAG_CLEAN = $(C_YELLOW)[CLEAN]$(C_RESET)
TAG_EXEC  = $(C_CYAN)[EXEC ]$(C_RESET)
TAG_TEST  = $(C_RED)[TEST ]$(C_RESET)

# Compiler settings
CC = clang

# --- Directories Structure ---
APP_DIR      = src
TEST_DIR     = test
LIB_DIR      = libds

OBJDIR = obj
BINDIR = bin

# --- Files Definition ---

# 1. Main Project Files (OS App)
APP_SOURCES  = $(wildcard $(APP_DIR)/*.c)
APP_OBJECTS  = $(patsubst $(APP_DIR)/%.c, $(OBJDIR)/app/%.o, $(APP_SOURCES))

# 2. Dictionary Library Files (Implementazione in libds)
LIB_SOURCES  = $(wildcard $(LIB_DIR)/*.c)
LIB_OBJECTS  = $(patsubst $(LIB_DIR)/%.c, $(OBJDIR)/lib/%.o, $(LIB_SOURCES))

# 3. Test Runner Files (Main di test in test/)
TEST_SRC     = $(TEST_DIR)/main.c
TEST_OBJ     = $(patsubst $(TEST_DIR)/%.c, $(OBJDIR)/test/%.o, $(TEST_SRC))


# --- Output Executables ---
APP_EXEC  = $(BINDIR)/os_app
TEST_EXEC = $(BINDIR)/test_dict_runner

# --- Flags Configuration ---

# Include paths: Include src/ e libds/
BASE_FLAGS = -Wno-fixed-enum-extension -Wall -Wextra -Wpedantic -Wconversion -D_GNU_SOURCE -I$(APP_DIR) -I$(LIB_DIR)

# Debug flags
DEBUG_FLAGS = -g -fsanitize=address,undefined -fno-omit-frame-pointer

# Production flags
PROD_FLAGS = -O3 -march=native -flto -DNDEBUG

# Default CFLAGS
CFLAGS = $(BASE_FLAGS) $(DEBUG_FLAGS)

# --- Targets ---

.PHONY: all clean rebuild production run test-lib test-lib-debug test-lib-prod

all: $(APP_EXEC)
	@echo "$(TAG_BUILD) OS Application compiled successfully."

production: CFLAGS = $(BASE_FLAGS) $(PROD_FLAGS)
production: clean $(APP_EXEC)
	@echo "$(TAG_BUILD) Production release compiled."

test-lib-debug-build: $(TEST_EXEC)

test-lib-prod-build: CFLAGS = $(BASE_FLAGS) $(PROD_FLAGS)
test-lib-prod-build: $(TEST_EXEC)

# --- Linking Rules ---

# Link Main App (App Objects + Lib Objects)
$(APP_EXEC): $(APP_OBJECTS) $(LIB_OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_BUILD) Linking OS App..."
	@$(CC) $(CFLAGS) $(APP_OBJECTS) $(LIB_OBJECTS) -o $(APP_EXEC)

# Link Test Runner (Test Main + Lib Objects)
$(TEST_EXEC): $(TEST_OBJ) $(LIB_OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_TEST) Linking Dictionary Stress Test..."
	@$(CC) $(CFLAGS) $(TEST_OBJ) $(LIB_OBJECTS) -o $(TEST_EXEC)

# --- Compilation Rules ---

# Compile App Sources
$(OBJDIR)/app/%.o: $(APP_DIR)/%.c
	@mkdir -p $(OBJDIR)/app
	@echo "$(TAG_BUILD) Compiling App: $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile Library Sources (libds)
$(OBJDIR)/lib/%.o: $(LIB_DIR)/%.c
	@mkdir -p $(OBJDIR)/lib
	@echo "$(TAG_BUILD) Compiling Library: $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile Test Runner (test)
$(OBJDIR)/test/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(OBJDIR)/test
	@echo "$(TAG_BUILD) Compiling Test Runner: $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# --- Utility Targets ---

clean:
	@rm -rf $(OBJDIR) $(BINDIR)
	@rm -rf *.dSYM
	@echo "$(TAG_CLEAN) All artifacts removed."

rebuild: clean all

run: $(APP_EXEC)
	@echo "$(TAG_EXEC) Running OS Application:\n"
	@./$(APP_EXEC)
	@echo "\n$(TAG_EXEC) Finished."

test-lib-debug: clean test-lib-debug-build
	@echo "$(TAG_TEST) Running Dictionary Stress Test [DEBUG MODE]:\n"
	@./$(TEST_EXEC)
	@echo "\n$(TAG_TEST) Test Completed."

test-lib-prod: clean test-lib-prod-build
	@echo "$(TAG_TEST) Running Dictionary Stress Test [PRODUCTION MODE - O3]:\n"
	@./$(TEST_EXEC)
	@echo "\n$(TAG_TEST) Test Completed."

test-lib: test-lib-prod