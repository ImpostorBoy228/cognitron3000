CFLAGS = $(shell pkg-config --cflags json-c telebot)
LIBS = $(shell pkg-config --libs json-c telebot) -lcurl

all: main agent

main: stream.c
	gcc stream.c -o stream $(CFLAGS) $(LIBS)

agent: agent.c nostream.c
	gcc agent.c nostream.c -o agent $(CFLAGS) $(LIBS)

clean: 
	rm -f *.o stream agent nostream

.PHONY: all main agent
