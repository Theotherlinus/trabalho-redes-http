#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h> // para gethostbyname
#include <arpa/inet.h>

#define BUFFER_SIZE 4096

// Função para extrair o nome do arquivo do caminho
const char *get_filename_from_path(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if (last_slash == NULL || *(last_slash + 1) == '\0') {
        return "index.html"; // Se for / ou vazio, salva como index.html
    }
    return last_slash + 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s http://host:porta/caminho\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *url = argv[1];
    char host[256];
    char path[1024];
    int port = 80; // Porta padrão HTTP

    // Parsear o URL
    // Formato 1: http://host:porta/caminho
    if (sscanf(url, "http://%99[^:]:%i%199[^\n]", host, &port, path) != 3) {
        // Formato 2: http://host/caminho (porta 80)
        if (sscanf(url, "http://%99[^/]%199[^\n]", host, path) != 2) {
            fprintf(stderr, "URL mal formatado.\n");
            exit(EXIT_FAILURE);
        }
        port = 80;
    }
    
    // Obter o nome do arquivo para salvar
    const char *filename = get_filename_from_path(path);
    printf("Host: %s, Porta: %d, Caminho: %s, Salvando como: %s\n", host, port, path, filename);

    // 1. Resolução de Host
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "ERRO: Host não encontrado %s\n", host);
        exit(EXIT_FAILURE);
    }

    // 2. Criar Socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERRO ao abrir socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    // 3. Conectar
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERRO ao conectar");
        exit(EXIT_FAILURE);
    }

    // 4. Enviar Requisição HTTP
    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE,
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n\r\n",
             path, host);
    
    if (write(sockfd, request, strlen(request)) < 0) {
        perror("ERRO ao escrever no socket");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 5. Ler a resposta (primeiro, checar o status line)
    char status_buffer[BUFFER_SIZE];
    char *line_end;
    ssize_t n_read = read(sockfd, status_buffer, BUFFER_SIZE - 1);
    if (n_read <= 0) {
         fprintf(stderr, "ERRO: Não recebeu resposta do servidor.\n");
         close(sockfd);
         exit(EXIT_FAILURE);
    }
    status_buffer[n_read] = '\0';
    
    // Verificar se é 200 OK
    if (strncmp(status_buffer, "HTTP/1.1 200 OK", 15) != 0 && 
        strncmp(status_buffer, "HTTP/1.0 200 OK", 15) != 0) {
        
        line_end = strstr(status_buffer, "\r\n");
        if (line_end) *line_end = '\0';
        fprintf(stderr, "Erro do servidor: %s\n", status_buffer);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Encontrar o fim dos cabeçalhos na primeira leitura
    char *header_end = strstr(status_buffer, "\r\n\r\n");
    if (header_end == NULL) {
        fprintf(stderr, "Resposta do servidor muito grande ou mal formatada (cabeçalhos).\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Apontar para o início do corpo
    char *body_start = header_end + 4;
    ssize_t body_len_in_buffer = n_read - (body_start - status_buffer);


    // 6. Abrir arquivo para salvar
    FILE *output_file = fopen(filename, "wb"); // "wb" para binário (imagens, pdf)
    if (output_file == NULL) {
        perror("ERRO ao criar arquivo de saída");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Escrever o que já lemos do corpo no buffer
    if (body_len_in_buffer > 0) {
        fwrite(body_start, 1, body_len_in_buffer, output_file);
    }

    // 7. Ler o resto do corpo (se houver) e salvar
    char read_buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(sockfd, read_buffer, BUFFER_SIZE)) > 0) {
        fwrite(read_buffer, 1, bytes_read, output_file);
    }

    fclose(output_file);
    close(sockfd);

    printf("Arquivo salvo com sucesso como '%s'.\n", filename);
    return 0;
}
