all:
	gcc main.c libs/cjson/cJSON.c -o main -I libs/cjson -lcurl
