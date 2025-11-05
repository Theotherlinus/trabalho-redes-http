#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 2048

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "uso: %s http://host:porta/caminho\n", argv[0]);
        exit(1);
    }

    char *url = argv[1];
    char host[256];
    char path[1024];
    int port = 80; // porta padrao

    // parsear url
    if (sscanf(url, "http://%99[^:]:%i%199[^\n]", host, &port, path) != 3) {
        if (sscanf(url, "http://%99[^/]%199[^\n]", host, path) != 2) {
            fprintf(stderr, "url mal formatado.\n");
            exit(1);
        }
        port = 80;
    }

    // escolher nome do arquivo
    char *filename_ptr = strrchr(path, '/');
    char *filename;
    if (filename_ptr == NULL || *(filename_ptr + 1) == '\0') {
        filename = "index.html"; // usa index.html se caminho terminar em /
    } else {
        filename = filename_ptr + 1; // pega o texto apos a ultima /
    }

    printf("host: %s, porta: %d, path: %s\n", host, port, path);

    // resolver host
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "erro: host nao encontrado.\n");
        exit(1);
    }

    // criar socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    // conectar
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("erro ao conectar");
        exit(1);
    }

    // enviar requisicao HTTP/1.1
    char request[2048];
    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    write(sockfd, request, strlen(request));

    // ler resposta (cabecalho + inicio do corpo)
    char header_buffer[BUFFER_SIZE];
    ssize_t n_read = read(sockfd, header_buffer, BUFFER_SIZE - 1);

    if (n_read <= 0) {
        fprintf(stderr, "erro: nao recebeu resposta.\n");
        close(sockfd);
        exit(1);
    }
    header_buffer[n_read] = '\0';

    // encontrar fim dos cabecalhos
    char *body_start = strstr(header_buffer, "\r\n\r\n");
    if (body_start == NULL) {
        fprintf(stderr, "erro: cabecalho mal formatado.\n");
        close(sockfd);
        exit(1);
    }

    // checar status rapido
    if (strncmp(header_buffer, "HTTP/1.1 200 OK", 15) != 0) {
        printf("aviso: servidor nao retornou '200 OK'.\n");
    }

    // pular cabecalhos
    body_start += 4; // pula "\r\n\r\n"

    // quanto do corpo ja esta no buffer
    ssize_t body_len_in_buffer = n_read - (body_start - header_buffer);

    // salvar arquivo
    FILE *output_file = fopen(filename, "wb");
    if (output_file == NULL) {
        perror("erro ao criar arquivo de saida");
        close(sockfd);
        exit(1);
    }

    if (body_len_in_buffer > 0) {
        fwrite(body_start, 1, body_len_in_buffer, output_file);
    }

    // ler o restante e gravar
    char read_buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(sockfd, read_buffer, BUFFER_SIZE)) > 0) {
        fwrite(read_buffer, 1, bytes_read, output_file);
    }

    fclose(output_file);
    close(sockfd);

    printf("arquivo salvo como '%s'.\n", filename);
    return 0;
}