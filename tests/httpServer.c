#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

int main() {
  int server_fd, client_fd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_len = sizeof(client_addr);
  char buffer[BUFFER_SIZE];

  server_fd = socket(AF_INET, SOCK_STREAM, 0);

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(8080);

  bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  listen(server_fd, 5);

  printf("服务器运行在 http://localhost:8080\n\n");

  while (1) {
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

    memset(buffer, 0, sizeof(buffer));
    read(client_fd, buffer, sizeof(buffer) - 1);

    printf("%s\n\n",buffer);

    // 返回响应
    char *response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
    write(client_fd, response, strlen(response));

    close(client_fd);
  }

  close(server_fd);
  return 0;
}