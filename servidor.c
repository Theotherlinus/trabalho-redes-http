#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>

#define PORT 8080
#define BUFFER_SIZE 4096

// Função para enviar resposta 404 Not Found
void send_404(int client_socket) {
    char *response = "HTTP/1.1 404 Not Found\r\n"
                     "Content-Type: text/html\r\n"
                     "Connection: close\r\n\r\n"
                     "<html><body><h1>404 Not Found</h1></body></html>";
    write(client_socket, response, strlen(response));
}

// Função para enviar um arquivo
void send_file(int client_socket, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (file == NULL) {
        send_404(client_socket);
        return;
    }

    // Obter o tamanho do arquivo
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Enviar cabeçalhos
    char header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n\r\n", // TODO: Adicionar Content-Type se tiver tempo
             file_size);
    write(client_socket, header, strlen(header));

    // Enviar conteúdo do arquivo em chunks
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        write(client_socket, buffer, bytes_read);
    }
    fclose(file);
}

// Função para listar um diretório
void list_directory(int client_socket, const char *dirpath, const char* relative_path) {
    DIR *dir = opendir(dirpath);
    if (dir == NULL) {
        send_404(client_socket);
        return;
    }

    char response_body[BUFFER_SIZE * 4] = {0}; // Buffer grande para o HTML
    snprintf(response_body, sizeof(response_body),
             "<html><head><title>Index of %s</title></head>"
             "<body><h1>Index of %s</h1><ul>",
             relative_path, relative_path);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue; // Ignorar .

        // Garante que o link não tenha barras duplas se relative_path for "/"
        char link_path[1024];
        if (strcmp(relative_path, "/") == 0) {
            snprintf(link_path, sizeof(link_path), "%s%s", relative_path, entry->d_name);
        } else {
            snprintf(link_path, sizeof(link_path), "%s/%s", relative_path, entry->d_name);
        }

        snprintf(response_body + strlen(response_body),
                 sizeof(response_body) - strlen(response_body),
                 "<li><a href=\"%s\">%s</a></li>",
                 link_path, entry->d_name);
    }
    closedir(dir);
    strcat(response_body, "</ul></body></html>");

    // Enviar cabeçalhos + corpo HTML
    char header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n\r\n",
             strlen(response_body));

    write(client_socket, header, strlen(header));
    write(client_socket, response_body, strlen(response_body));
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <diretorio_raiz>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *root_dir = argv[1];

    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Criar socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Configurar socket para reutilizar porta
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Servidor rodando em http://localhost:%d/ servindo de %s\n", PORT, root_dir);

    // Loop principal de aceite
    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue; // Continua para o próximo cliente
        }

        char buffer[BUFFER_SIZE] = {0};
        read(client_socket, buffer, BUFFER_SIZE - 1);

        // Extrair o caminho da requisição (ex: GET /path/file.html HTTP/1.1)
        char method[16], path[1024], protocol[16];
        if (sscanf(buffer, "%s %s %s", method, path, protocol) < 3) {
            close(client_socket);
            continue;
        }

        printf("Requisição recebida: %s %s\n", method, path);

        // Construir caminho completo do arquivo no sistema
        char file_path[2048];
        snprintf(file_path, sizeof(file_path), "%s%s", root_dir, path);

        // Verificar se o caminho é um diretório
        struct stat path_stat;
        stat(file_path, &path_stat);

        if (S_ISDIR(path_stat.st_mode)) {
            // É um diretório, verificar se 'index.html' existe
            char index_path[2048];
            snprintf(index_path, sizeof(index_path), "%s/index.html", file_path);
            
            if (access(index_path, F_OK) == 0) {
                // index.html existe, enviar ele
                send_file(client_socket, index_path);
            } else {
                // index.html não existe, listar o diretório
                list_directory(client_socket, file_path, path);
            }
        } else if (S_ISREG(path_stat.st_mode)) {
            // É um arquivo regular, enviar o arquivo
            send_file(client_socket, file_path);
        } else {
            // Não encontrado
            send_404(client_socket);
        }

        close(client_socket);
    }

    return 0;
}
