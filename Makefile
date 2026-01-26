# ==========================================
#   Project Makefile (Auto-Discovery Mode)
#   Compiler: gcc / clang
# ==========================================

TAG_BUILD = [BUILD]
TAG_CLEAN = [CLEAN]
TAG_EXEC  = [EXEC ]
TAG_TEST  = [TEST ]

CC = gcc

# --- Directories ---
APP_DIR      = src
TEST_DIR     = test
LIB_DIR      = libds
OBJDIR       = obj
BINDIR       = bin

# --- 1. Gestione Intelligente dei Sorgenti ---

# A. Trova TUTTI i file .c in src/
ALL_APP_SOURCES = $(wildcard $(APP_DIR)/*.c)

# B. Lista dei file che contengono il 'main' (Entry Points)
# Questi NON devono essere linkati tra loro!
MAIN_SRC      = $(APP_DIR)/main.c
WORKER_SRC    = $(APP_DIR)/worker.c
CLIENT_SRC    = $(APP_DIR)/client.c
DISORDER_SRC  = $(APP_DIR)/disorder.c
EXCLUDED_SRCS = $(MAIN_SRC) $(WORKER_SRC) $(CLIENT_SRC) $(DISORDER_SRC)

# C. Calcola i file Comuni (Sottrae i Main da Tutti i sorgenti)
# Se aggiungi menu.c o utils.c, finiscono qui automaticamente.
COMMON_SOURCES = $(filter-out $(EXCLUDED_SRCS), $(ALL_APP_SOURCES))
COMMON_OBJECTS = $(patsubst $(APP_DIR)/%.c, $(OBJDIR)/app/%.o, $(COMMON_SOURCES))

# D. Definisci gli oggetti degli Entry Points
OBJ_MAIN     = $(OBJDIR)/app/main.o
OBJ_WORKER   = $(OBJDIR)/app/worker.o
OBJ_CLIENT   = $(OBJDIR)/app/client.o
OBJ_DISORDER = $(OBJDIR)/app/disorder.o 

# E. Libreria Esterna (libds)
LIB_SOURCES = $(wildcard $(LIB_DIR)/*.c)
LIB_OBJECTS = $(patsubst $(LIB_DIR)/%.c, $(OBJDIR)/lib/%.o, $(LIB_SOURCES))

# F. Test
TEST_SRC    = $(TEST_DIR)/main.c
TEST_OBJ    = $(patsubst $(TEST_DIR)/%.c, $(OBJDIR)/test/%.o, $(TEST_SRC))

# --- Eseguibili Finali ---
EXEC_MAIN     = $(BINDIR)/main
EXEC_WORKER   = $(BINDIR)/worker
EXEC_CLIENT   = $(BINDIR)/client
EXEC_DISORDER = $(BINDIR)/disorder 
TEST_EXEC     = $(BINDIR)/test_dict_runner

# --- Flags ---
BASE_FLAGS  = -Wall -Wextra -Wpedantic -D_GNU_SOURCE -I$(APP_DIR) -I$(LIB_DIR)
DEBUG_FLAGS = -g -fsanitize=address,undefined -fno-omit-frame-pointer
PROD_FLAGS  = -O3 -march=native -flto -DNDEBUG
CFLAGS      = $(BASE_FLAGS) $(DEBUG_FLAGS)

# --- Targets ---

.PHONY: all clean rebuild production run-main run-worker run-client

all: $(EXEC_MAIN) $(EXEC_WORKER) $(EXEC_CLIENT) $(EXEC_DISORDER)
	@echo "$(TAG_BUILD) Project compiled successfully."

production: CFLAGS = $(BASE_FLAGS) $(PROD_FLAGS)
production: clean all
	@echo "$(TAG_BUILD) Production release ready."

# --- Linking Rules (Automatizzati) ---

# Link Main: Oggetto Main + Oggetti Comuni (menu.o, etc) + Libreria
$(EXEC_MAIN): $(OBJ_MAIN) $(COMMON_OBJECTS) $(LIB_OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_BUILD) Linking MAIN (with common files)..."
	@$(CC) $(CFLAGS) $^ -o $@

# Link Worker: Oggetto Worker + Oggetti Comuni + Libreria
$(EXEC_WORKER): $(OBJ_WORKER) $(COMMON_OBJECTS) $(LIB_OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_BUILD) Linking WORKER (with common files)..."
	@$(CC) $(CFLAGS) $^ -o $@

# Link Client: Oggetto Client + Oggetti Comuni + Libreria
$(EXEC_CLIENT): $(OBJ_CLIENT) $(COMMON_OBJECTS) $(LIB_OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_BUILD) Linking CLIENT (with common files)..."
	@$(CC) $(CFLAGS) $^ -o $@

$(EXEC_DISORDER): $(OBJ_DISORDER) $(COMMON_OBJECTS) $(LIB_OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_BUILD) Linking DISORDER (with common files)..."
	@$(CC) $(CFLAGS) $^ -o $@
	
# Link Test
$(TEST_EXEC): $(TEST_OBJ) $(LIB_OBJECTS)
	@mkdir -p $(BINDIR)
	@echo "$(TAG_TEST) Linking Test Runner..."
	@$(CC) $(CFLAGS) $^ -o $@

# --- Compilation Rules ---

# Compila qualsiasi .c in src/ dentro obj/app/
$(OBJDIR)/app/%.o: $(APP_DIR)/%.c
	@mkdir -p $(OBJDIR)/app
	@echo "$(TAG_BUILD) Compiling: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compila libreria
$(OBJDIR)/lib/%.o: $(LIB_DIR)/%.c
	@mkdir -p $(OBJDIR)/lib
	@echo "$(TAG_BUILD) Compiling Lib: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compila test
$(OBJDIR)/test/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(OBJDIR)/test
	@echo "$(TAG_BUILD) Compiling Test: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# --- Utilities ---

clean:
	@rm -rf $(OBJDIR) $(BINDIR)
	@echo "$(TAG_CLEAN) Cleaned."

run-main: $(EXEC_MAIN)
	@./$(EXEC_MAIN)

run-worker: $(EXEC_WORKER)
	@./$(EXEC_WORKER)

run-client: $(EXEC_CLIENT)
	@./$(EXEC_CLIENT)

run-disorder: $(EXEC_DISORDER)
	@./$(EXEC_DISORDER)
