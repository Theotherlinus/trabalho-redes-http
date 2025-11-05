#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BACKLOG 10
#define BUF_SZ 8192

// retorna o tipo mime basico com base na extensao do nome
// esta funcao usa apenas alguns tipos comuns para simplificar

static const char *get_mime(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    return "application/octet-stream";
}

void send_404(int fd) {
    // envia uma resposta 404 simples em texto plano
    // aqui mantemos a linha de status padrao do http
    const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\narquivo nao encontrado.";
    send(fd, resp, strlen(resp), 0);
}

void send_500(int fd) {
    // envia resposta 500 indicando erro interno no servidor
    // usado quando alguma chamada de sistema falha
    const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nerro interno do servidor.";
    send(fd, resp, strlen(resp), 0);
}

// gera e envia uma pagina html com a lista de arquivos
// cada arquivo vira um link para download
void send_index(int fd) {
    DIR *dir = opendir(".");
    if (!dir) { send_500(fd); return; }

    // cabeçalho http indicando sucesso e conteudo html
    const char *hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n";
    send(fd, hdr, strlen(hdr), 0);

    // inicio do html com titulo e lista
    send(fd, "<!doctype html><html><head><meta charset='utf-8'><title>Arquivos</title></head><body>", 86, 0);
    send(fd, "<h1>Arquivos disponiveis</h1><ul>", 34, 0);

    struct dirent *entry;
    char line[1024];
    while ((entry = readdir(dir)) != NULL) {
        // ignora entradas especiais
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // monta a linha de lista com link para o arquivo
        snprintf(line, sizeof(line),
                 "<li><a href=\"/%s\" download>%s</a></li>",
                 entry->d_name, entry->d_name);
        send(fd, line, strlen(line), 0);
    }

    closedir(dir);
    send(fd, "</ul></body></html>", 20, 0);
}

// trata e envia um arquivo solicitado
// validacoes simples: remove barra inicial, evita path traversal, abre e envia
void send_file(int fd, const char *path) {
    const char *p = path;
    // remove barra inicial se houver
    if (p[0] == '/') p++;
    // se caminho vazio, envia a pagina index
    if (p[0] == '\0') { send_index(fd); return; }

    // evita navegacao para diretorios pai
    if (strstr(p, "..")) { send_404(fd); return; }

    // abre o arquivo para leitura
    int f = open(p, O_RDONLY);
    if (f < 0) { send_404(fd); return; }

    struct stat st;
    if (fstat(f, &st) < 0) { close(f); send_500(fd); return; }

    // monta e envia os cabecalhos http (content-type, content-length)
    char header[512];
    const char *mime = get_mime(p);
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
             "Content-Disposition: attachment; filename=\"%s\"\r\n\r\n",
             mime, (long)st.st_size, p);
    send(fd, header, strlen(header), 0);

    // envia o corpo em blocos de ate BUF_SZ bytes
    char buf[BUF_SZ];
    ssize_t r;
    while ((r = read(f, buf, sizeof(buf))) > 0)
        send(fd, buf, r, 0);
    close(f);
}

int main(int argc, char **argv) {
    // porta padrao e pasta servida
    int port = 8080;
    const char *folder = ".";

    // se o usuario passou um diretorio existente, usamos ele como pasta raiz
    if (argc > 1 && access(argv[1], F_OK) == 0) {
        folder = argv[1];
        chdir(folder);
    }

    // se o usuario passou a porta como segundo argumento, usamos ela
      if (argc > 2) {
        int temp = atoi(argv[2]);
        if (temp > 0) port = temp;
    }

    // cria o socket tcp
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    // associa o socket a porta escolhida
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    // comeca a escutar conexoes
    if (listen(sockfd, BACKLOG) < 0) {
        perror("listen");
        return 1;
    }

    // imprime informacao basica no terminal
    printf("servidor http rodando na porta %d, servindo pasta: %s\n", port, folder);

    // loop principal: aceita conexoes e processa uma por vez
    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int client = accept(sockfd, (struct sockaddr*)&cli, &clilen);
        if (client < 0) continue;

        // le a requisicao do cliente
        char req[4096];
        ssize_t len = recv(client, req, sizeof(req)-1, 0);
        if (len <= 0) { close(client); continue; }
        req[len] = 0;

        // extrai o metodo e o path da primeira linha
        char method[8], path[1024];
        if (sscanf(req, "%7s %1023s", method, path) != 2) {
            close(client);
            continue;
        }

        // aceita apenas get
        if (strcmp(method, "GET") != 0) {
            const char *m = "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\n\r\n";
            send(client, m, strlen(m), 0);
            close(client);
            continue;
        }

        // rota basica: index ou arquivo solicitado
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
            send_index(client);
        else
            send_file(client, path);

        // fecha a conexao apos responder
        close(client);
    }

    close(sockfd);
    return 0;
}
