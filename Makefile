all: opener

opener: opener.c direntry.c
	$(CC) $(FLAGS) -Wall -Wextra -pedantic -pipe -g $^ -o $@

clean:
	rm -f opener

install: opener
	cp -f opener /usr/local/bin/opener

uninstall:
	rm -f /usr/local/bin/opener

.PHONY: all clean install uninstall
