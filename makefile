server, client: server.c client.c
	gcc -o server server.c
	gcc -o client client.c
	
clean:
	rm -f server client