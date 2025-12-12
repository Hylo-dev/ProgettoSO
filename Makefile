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
DICT_DIR     = dict
DICT_INC_DIR = $(DICT_DIR)/include
DICT_SRC_DIR = $(DICT_DIR)/src

OBJDIR = obj
BINDIR = bin

# --- Files Definition ---

# 1. Main Project Files (OS Project)
APP_SOURCES  = $(wildcard $(APP_DIR)/*.c)
APP_OBJECTS  = $(patsubst $(APP_DIR)/%.c, $(OBJDIR)/app/%.o, $(APP_SOURCES))

# 2. Dictionary Library Files
# Prendiamo tutto quello che c'è in dict/src
ALL_DICT_SRCS = $(wildcard $(DICT_SRC_DIR)/*.c)

# Identifichiamo il main di test della libreria per gestirlo separatamente
DICT_TEST_SRC = $(DICT_SRC_DIR)/main.c

# I sorgenti della libreria vera e propria sono tutto MENO il main di test
DICT_LIB_SRCS = $(filter-out $(DICT_TEST_SRC), $(ALL_DICT_SRCS))

# Oggetti della libreria (senza main test)
DICT_LIB_OBJS = $(patsubst $(DICT_SRC_DIR)/%.c, $(OBJDIR)/dict/%.o, $(DICT_LIB_SRCS))

# Oggetto del main di test (specifico per il target di debug library)
DICT_TEST_OBJ = $(patsubst $(DICT_SRC_DIR)/%.c, $(OBJDIR)/dict/%.o, $(DICT_TEST_SRC))


# --- Output Executables ---
APP_EXEC  = $(BINDIR)/os_app
TEST_EXEC = $(BINDIR)/test_dict_runner

# --- Flags Configuration ---

# Include paths: Cerca in src/ e in dict/include/
BASE_FLAGS = -Wfixed-enum-extension -Wall -Wextra -Wpedantic -Wconversion -D_GNU_SOURCE \
             -I$(APP_DIR) -I$(DICT_INC_DIR)

# Debug: Address Sanitizer attivato per trovare memory leaks e buffer overflow
DEBUG_FLAGS = -g -fsanitize=address,undefined -fno-omit-frame-pointer

# Production: Massima velocità
PROD_FLAGS = -O3 -march=native -flto -DNDEBUG

# Default CFLAGS (Debug mode)
CFLAGS = $(BASE_FLAGS) $(DEBUG_FLAGS)

# --- Targets ---

.PHONY: all clean rebuild production run test-lib test-lib-debug test-lib-prod

# 1. Default: Builds the Main OS App (linking with Dict Lib)
all: $(APP_EXEC)
	@echo "$(TAG_BUILD) OS Application compiled successfully."

# 2. Production Build
production: CFLAGS = $(BASE_FLAGS) $(PROD_FLAGS)
production: clean $(APP_EXEC)
	@echo "$(TAG_BUILD) Production release compiled."

# 3. Test Lib Build (DEBUG mode - default)
test-lib-debug-build: $(TEST_EXEC)

# 4. Test Lib Build (PRODUCTION mode - O3)
test-lib-prod-build: CFLAGS = $(BASE_FLAGS) $(PROD_FLAGS)
test-lib-prod-build: $(TEST_EXEC)

# --- Linking Rules ---

# Link Main App (App Objects + Lib Objects)
$(APP_EXEC): $(APP_OBJECTS) $(DICT_LIB_OBJS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_BUILD) Linking OS App..."
	@$(CC) $(CFLAGS) $(APP_OBJECTS) $(DICT_LIB_OBJS) -o $(APP_EXEC)

# Link Test Runner (Lib Test Main + Lib Objects)
$(TEST_EXEC): $(DICT_TEST_OBJ) $(DICT_LIB_OBJS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_TEST) Linking Dictionary Stress Test..."
	@$(CC) $(CFLAGS) $(DICT_TEST_OBJ) $(DICT_LIB_OBJS) -o $(TEST_EXEC)

# --- Compilation Rules ---

# Compile App Sources
$(OBJDIR)/app/%.o: $(APP_DIR)/%.c
	@mkdir -p $(OBJDIR)/app
	@echo "$(TAG_BUILD) Compiling App: $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile Dictionary Sources (sia dict.c che main.c finiscono qui)
$(OBJDIR)/dict/%.o: $(DICT_SRC_DIR)/%.c
	@mkdir -p $(OBJDIR)/dict
	@echo "$(TAG_BUILD) Compiling Lib: $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# --- Utility Targets ---

clean:
	@rm -rf $(OBJDIR) $(BINDIR)
	@rm -rf *.dSYM
	@echo "$(TAG_CLEAN) All artifacts removed."

rebuild: clean all

# Run Main OS App
run: $(APP_EXEC)
	@echo "$(TAG_EXEC) Running OS Application:\n"
	@./$(APP_EXEC)
	@echo "\n$(TAG_EXEC) Finished."

# Run Dictionary Stress Test (DEBUG Mode - con Sanitizer)
test-lib-debug: clean test-lib-debug-build
	@echo "$(TAG_TEST) Running Dictionary Stress Test [DEBUG MODE]:\n"
	@./$(TEST_EXEC)
	@echo "\n$(TAG_TEST) Test Completed."

# Run Dictionary Stress Test (PRODUCTION Mode - O3, nessun sanitizer)
test-lib-prod: clean test-lib-prod-build
	@echo "$(TAG_TEST) Running Dictionary Stress Test [PRODUCTION MODE - O3]:\n"
	@./$(TEST_EXEC)
	@echo "\n$(TAG_TEST) Test Completed."

# Alias: test-lib punta a production per default (come richiesto)
test-lib: test-lib-prod