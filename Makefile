CC ?= cc

CFLAGS := -std=c11 -Wall -Wextra -O2 -Iinclude

ifeq ($(OS),Windows_NT)
TARGET := lovelang.exe
BIN := lovelang.exe
else
TARGET := lovelang
BIN := ./lovelang
endif

SRC := src/main.c \
	src/lexer.c \
	src/parser.c \
	src/compiler.c \
	src/codegen.c \
	src/codegen_x64.c \
	src/runtime.c

.PHONY: all clean run install

all: $(TARGET)

$(TARGET): $(SRC) include/love.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
	@echo "  built: $(BIN)"

run: $(TARGET)
ifeq ($(OS),Windows_NT)
	cmd /c run_love.cmd examples\\01-romantic-hello.love
else
	$(BIN) examples/01-romantic-hello.love
endif

ifeq ($(OS),Windows_NT)
install: $(TARGET)
	@echo "  Windows build ready: $(TARGET)"
	@echo "  Add this folder to PATH or copy $(TARGET) into a PATH directory."
else
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/lovelang
	@echo "  installed to /usr/local/bin/lovelang"
endif

clean:
ifeq ($(OS),Windows_NT)
	-cmd /c if exist lovelang.exe del /f /q lovelang.exe
	-cmd /c if exist lovelang del /f /q lovelang
else
	rm -f lovelang lovelang.exe
endif
