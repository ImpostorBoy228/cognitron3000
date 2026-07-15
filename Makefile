all: main agent

main: main.c libs/cjson/cJSON.c
	gcc main.c libs/cjson/cJSON.c -o main -I libs/cjson -lcurl

agent: agent.c nostream.c libs/cjson/cJSON.c
	gcc agent.c nostream.c libs/cjson/cJSON.c -o agent -I libs/cjson -lcurl

.PHONY: all main agent
