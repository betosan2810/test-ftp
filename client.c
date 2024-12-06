// client.c
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#define BUFFER_SIZE 1024

// Hàm gửi lệnh và nhận phản hồi
void send_command(int sock, const char *cmd) {
    send(sock, cmd, strlen(cmd), 0);
}

// Hàm nhận và hiển thị phản hồi từ server với dấu thời gian
void receive_response(int sock, const char *command_sent) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_received = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        
        // Lấy thời gian hiện tại
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[20];
        strftime(time_str, sizeof(time_str)-1, "%H:%M:%S", t);
        
        // Hiển thị phản hồi
        printf("[%s] %s: %s", time_str, command_sent, buffer);
    }
}

// Hàm hiển thị prompt với thời gian và thư mục hiện tại
void print_prompt(const char *current_dir) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str)-1, "%H:%M:%S", t);
    printf("[%s] (%s) $ ", time_str, current_dir);
}

// Hàm tải file từ server (RETR)
void retrieve_file(int sock, const char *filename) {
    char command[BUFFER_SIZE];
    sprintf(command, "RETR %s\r\n", filename);
    send_command(sock, command);
    receive_response(sock, "RETR");

    // Tạo file để lưu dữ liệu từ server
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Không thể mở file để lưu");
        return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    while ((bytes_received = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
    }
    fclose(file);
    printf("Đã tải file %s từ server.\n", filename);
}

// Hàm tải file lên server (STOR)
void store_file(int sock, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Không thể mở file để tải lên");
        return;
    }

    char command[BUFFER_SIZE];
    sprintf(command, "STOR %s\r\n", filename);
    send_command(sock, command);
    receive_response(sock, "STOR");

    // Đọc file và gửi từng phần dữ liệu lên server
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }

    fclose(file);
    printf("Đã tải file %s lên server.\n", filename);
}

int main(int argc, const char *argv[]) {
    if (argc != 3) {
        printf("Sử dụng: %s <địa_chỉ_IP_server> <cổng_server>\n", argv[0]);
        return 0;
    }

    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char current_dir[512] = "/";

    // Tạo socket
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Không thể tạo socket");
        exit(errno);
    }

    // Cấu hình địa chỉ máy chủ
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    memset(&(server_addr.sin_zero), '\0', 8);

    // Kết nối tới máy chủ
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("Kết nối thất bại");
        close(sock);
        exit(errno);
    }

    // Nhận thông báo chào mừng từ server
    receive_response(sock, "SERVER");

    // Đăng nhập
    char username[50], password[50];
    printf("Tên người dùng: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';
    char user_cmd[BUFFER_SIZE];
    sprintf(user_cmd, "USER %s\r\n", username);
    send_command(sock, user_cmd);
    receive_response(sock, "USER");

    printf("Mật khẩu: ");
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = '\0';
    char pass_cmd[BUFFER_SIZE];
    sprintf(pass_cmd, "PASS %s\r\n", password);
    send_command(sock, pass_cmd);
    receive_response(sock, "PASS");

    // Vòng lặp chính
    while (1) {
        print_prompt(current_dir);
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strlen(buffer) == 0) continue;

        // Kiểm tra lệnh đặc biệt
        if (strcasecmp(buffer, "exit") == 0) {
            char quit_cmd[] = "QUIT\r\n";
            send_command(sock, quit_cmd);
            receive_response(sock, "QUIT");
            break;
        }

        // Lệnh RETR (Download file)
        if (strncasecmp(buffer, "RETR", 4) == 0) {
            char *filename = strtok(buffer + 5, " \r\n");
            if (filename) {
                retrieve_file(sock, filename);
            }
            continue;
        }

        // Lệnh STOR (Upload file)
        if (strncasecmp(buffer, "STOR", 4) == 0) {
            char *filename = strtok(buffer + 5, " \r\n");
            if (filename) {
                store_file(sock, filename);
            }
            continue;
        }

        // Gửi lệnh tới server
        strcat(buffer, "\r\n");
        send_command(sock, buffer);
        receive_response(sock, buffer);

        // Xử lý lệnh cục bộ nếu cần
        // Ví dụ: cập nhật current_dir sau lệnh CWD
        char *cmd = strtok(buffer, " \r\n");
        if (cmd && strcasecmp(cmd, "CWD") == 0) {
            char *new_dir = strtok(NULL, " \r\n");
            if (new_dir) {
                // Giả sử server đã phản hồi thành công
                strcpy(current_dir, new_dir);
            }
        }
    }

    close(sock);
    printf("Đã ngắt kết nối.\n");
    return 0;
}
