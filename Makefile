CFLAGS = $(shell pkg-config --cflags json-c telebot)
LIBS = $(shell pkg-config --libs json-c telebot) -lcurl -Llibs -lmimalloc -lpthread -lsqlite3

all: main anton

main: stream.c
	gcc stream.c -o stream $(CFLAGS) $(LIBS)

anton: anton.c nostream.c
	gcc anton.c nostream.c -o anton $(CFLAGS) $(LIBS)

clean: 
	rm -f *.o stream anton nostream

.PHONY: all main anton
