CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Wno-unused-function -O2 -g
BISON   ?= bison
FLEX    ?= flex

BUILD   = build
SRC     = src
TARGET  = $(BUILD)/c2py

OBJS = $(BUILD)/parser.o $(BUILD)/lexer.o $(BUILD)/ast.o $(BUILD)/sema.o \
       $(BUILD)/interp.o $(BUILD)/codegen_py.o $(BUILD)/main.o

.PHONY: all clean test demo report conflicts

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

# bison: граматика -> parser.c + parser.h (+ звіт про стани LALR у parser.output)
$(BUILD)/parser.c $(BUILD)/parser.h: $(SRC)/parser.y | $(BUILD)
	$(BISON) -Wall --defines=$(BUILD)/parser.h --report=all --report-file=$(BUILD)/parser.output -o $(BUILD)/parser.c $<

# flex: регулярні вирази -> скінченний автомат у lexer.c
$(BUILD)/lexer.c: $(SRC)/lexer.l $(BUILD)/parser.h | $(BUILD)
	$(FLEX) -o $@ $<

$(BUILD)/%.o: $(BUILD)/%.c
	$(CC) $(CFLAGS) -Wno-sign-compare -I$(SRC) -I$(BUILD) -c -o $@ $<

$(BUILD)/%.o: $(SRC)/%.c $(BUILD)/parser.h $(wildcard $(SRC)/*.h)
	$(CC) $(CFLAGS) -I$(SRC) -I$(BUILD) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

# запуск усіх тестів: gcc vs інтерпретатор vs згенерований Python
test: $(TARGET)
	@./tests/run_tests.sh

# веб-візуалізація AST на http://localhost:5050
demo: $(TARGET)
	python3 server.py 5050

# звіт bison про стани автомата (для захисту теорії)
report: $(BUILD)/parser.c
	@echo "see $(BUILD)/parser.output"
	@grep -m1 -i "conflict" $(BUILD)/parser.output || echo "conflicts: none"

# граматики з навмисними конфліктами (docs/conflicts)
conflicts:
	@for g in docs/conflicts/*.y; do \
	    echo "===== $$g"; \
	    $(BISON) -Wcounterexamples -o /dev/null $$g 2>&1 | head -40; \
	done

clean:
	rm -rf $(BUILD)
