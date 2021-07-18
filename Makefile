NAME=opener
COMMON_FLAGS=-Wall -Wextra -Wpedantic -Wconversion -Wcast-qual -Wwrite-strings -Werror
LDFLAGS=
RFLAGS=-O2 -g -flto $(COMMON_FLAGS)
DFLAGS=-g -fanalyzer $(COMMON_FLAGS)

SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)

ROBJ = $(SRC:src/%.c=release/obj/%.o)
DOBJ = $(SRC:src/%.c=debug/obj/%.o)


release: release/$(NAME)

release/$(NAME): $(ROBJ)
	$(CC) $(RFLAGS) $(LDFLAGS) $^ -o $@

release/obj/%.o: src/%.c | release/obj
	$(CC) $(RFLAGS) $^ -c -o $@

release/obj:
	mkdir -p $@


debug: debug/$(NAME)

debug/$(NAME): $(DOBJ)
	$(CC) $(DFLAGS) $(LDFLAGS) $^ -o $@

debug/obj/%.o: src/%.c | debug/obj
	$(CC) $(DFLAGS) $^ -c -o $@

debug/obj:
	mkdir -p $@


format: $(SRC) $(HDR)
	clang-format -i --style=file $^

clean:
	rm -rf $(NAME) release debug

install: release
	cp -f release/$(NAME) /usr/local/bin/$(NAME)

uninstall:
	rm -f /usr/local/bin/$(NAME)

.PHONY: release debug format clean install uninstall
