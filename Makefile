# Define o compilador
CC = gcc

# Define flags de compilação (g = debug, Wall = todos os warnings, std=c99 para compatibilidade)
CFLAGS = -g -Wall -Wextra -std=c99 -o

# Nomes dos executáveis
CLIENT = meu_navegador
SERVER = meu_servidor

# Arquivos fonte
CLIENT_SRC = cliente.c
SERVER_SRC = servidor.c

# Regra principal: compila ambos
all: $(CLIENT) $(SERVER)

# Regra para compilar o cliente
$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT) $(CLIENT_SRC)

# Regra para compilar o servidor
$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SERVER) $(SERVER_SRC)

# Regra para limpar os arquivos compilados
clean:
	rm -f $(CLIENT) $(SERVER) *.o

