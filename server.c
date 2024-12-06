// server.c
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
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>

#define PORT 2121 // Cổng FTP thường là 21, nhưng cần quyền root. Sử dụng 2121 cho dễ
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define ACCOUNT_FILE "account.txt"

// Cấu trúc tài khoản
typedef struct Account {
    char username[50];
    char password[50];
    char root_dir[256];
    struct Account *next;
} Account;

// Hàm đọc tài khoản từ file
Account* load_accounts(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Không thể mở file tài khoản");
        return NULL;
    }

    Account *head = NULL;
    Account *current = NULL;
    char line[512];

    while (fgets(line, sizeof(line), file)) {
        Account *acc = (Account*)malloc(sizeof(Account));
        sscanf(line, "%[^,],%[^,],%s", acc->username, acc->password, acc->root_dir);
        acc->next = NULL;

        if (!head) {
            head = acc;
            current = acc;
        } else {
            current->next = acc;
            current = acc;
        }
    }

    fclose(file);
    return head;
}

// Hàm ghi log
void log_command(const char *cmd, struct sockaddr_in client_addr) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str)-1, "%H:%M:%S", t);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    printf("[%s] <%s> <%s>\n", time_str, cmd, client_ip);
}

// Hàm gửi phản hồi
void send_response(int client_sock, const char *response) {
    send(client_sock, response, strlen(response), 0);
}

// Hàm xử lý lệnh LIST
void handle_LIST(int client_sock, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        send_response(client_sock, "550 Không thể mở thư mục.\r\n");
        return;
    }

    struct dirent *entry;
    char response[BUFFER_SIZE];
    memset(response, 0, sizeof(response));

    while ((entry = readdir(dir)) != NULL) {
        // Bỏ qua . và ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        strcat(response, entry->d_name);
        strcat(response, "\n");
    }

    closedir(dir);
    send_response(client_sock, "150 Mở kết nối dữ liệu.\r\n");
    send(client_sock, response, strlen(response), 0);
    send_response(client_sock, "226 Danh sách thư mục đã gửi thành công.\r\n");
}

// Hàm xử lý lệnh CWD
void handle_CWD(int client_sock, char *current_dir, const char *new_dir) {
    char path[512];
    if (strcmp(new_dir, "..") == 0) {
        // Thay đổi lên thư mục cha
        char *last_slash = strrchr(current_dir, '/');
        if (last_slash && last_slash != current_dir) {
            *last_slash = '\0';
        }
    } else {
        // Thay đổi vào thư mục con
        if (strcmp(current_dir, "/") == 0)
            sprintf(path, "/%s", new_dir);
        else
            sprintf(path, "%s/%s", current_dir, new_dir);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            strcpy(current_dir, path);
        } else {
            send_response(client_sock, "550 Thư mục không tồn tại.\r\n");
            return;
        }
    }
    send_response(client_sock, "250 Thay đổi thư mục thành công.\r\n");
}

// Hàm xử lý lệnh PWD
void handle_PWD(int client_sock, const char *current_dir) {
    char response[512];
    sprintf(response, "257 \"%s\" là thư mục hiện tại.\r\n", current_dir);
    send_response(client_sock, response);
}

// Hàm xử lý lệnh RETR (download)
void handle_RETR(int client_sock, const char *filename, const char *current_dir) {
    char filepath[512];
    if (strcmp(current_dir, "/") == 0)
        sprintf(filepath, "/%s", filename);
    else
        sprintf(filepath, "%s/%s", current_dir, filename);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        send_response(client_sock, "550 Không thể mở tệp tin.\r\n");
        return;
    }

    send_response(client_sock, "150 Mở kết nối dữ liệu.\r\n");
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(client_sock, buffer, bytes, 0);
    }
    fclose(fp);
    send_response(client_sock, "226 Truyền tệp tin hoàn tất.\r\n");
}

// Hàm xử lý lệnh STOR (upload)
void handle_STOR(int client_sock, const char *filename, const char *current_dir) {
    char filepath[512];
    if (strcmp(current_dir, "/") == 0)
        sprintf(filepath, "/%s", filename);
    else
        sprintf(filepath, "%s/%s", current_dir, filename);

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        send_response(client_sock, "550 Không thể tạo tệp tin.\r\n");
        return;
    }

    send_response(client_sock, "150 Mở kết nối dữ liệu.\r\n");
    char buffer[BUFFER_SIZE];
    ssize_t bytes;
    while ((bytes = recv(client_sock, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, bytes, fp);
        // Giả sử client gửi dữ liệu và sau đó đóng kết nối dữ liệu
        // Để thực tế, cần có cách xác định khi nào dữ liệu kết thúc
    }
    fclose(fp);
    send_response(client_sock, "226 Truyền tệp tin hoàn tất.\r\n");
}

// Hàm xử lý các lệnh FTP
void handle_command(int client_sock, char *cmd, char *current_dir, struct sockaddr_in client_addr) {
    log_command(cmd, client_addr);
    char *token = strtok(cmd, " \r\n");
    if (!token) return;

    if (strcasecmp(token, "USER") == 0) {
        char *username = strtok(NULL, " \r\n");
        // Lưu trữ tên người dùng để xác thực sau này
        // Gửi phản hồi tạm thời
        send_response(client_sock, "331 Tên người dùng được chấp nhận, cần mật khẩu.\r\n");
    }
    else if (strcasecmp(token, "PASS") == 0) {
        char *password = strtok(NULL, " \r\n");
        // Xác thực tên người dùng và mật khẩu
        // Giả sử đã xác thực thành công
        send_response(client_sock, "230 Đăng nhập thành công.\r\n");
    }
    else if (strcasecmp(token, "LIST") == 0) {
        handle_LIST(client_sock, current_dir);
    }
    else if (strcasecmp(token, "CWD") == 0) {
        char *new_dir = strtok(NULL, " \r\n");
        if (new_dir)
            handle_CWD(client_sock, current_dir, new_dir);
        else
            send_response(client_sock, "550 Cần tên thư mục.\r\n");
    }
    else if (strcasecmp(token, "PWD") == 0) {
        handle_PWD(client_sock, current_dir);
    }
    else if (strcasecmp(token, "RETR") == 0) {
        char *filename = strtok(NULL, " \r\n");
        if (filename)
            handle_RETR(client_sock, filename, current_dir);
        else
            send_response(client_sock, "550 Cần tên tệp tin.\r\n");
    }
    else if (strcasecmp(token, "STOR") == 0) {
        char *filename = strtok(NULL, " \r\n");
        if (filename)
            handle_STOR(client_sock, filename, current_dir);
        else
            send_response(client_sock, "550 Cần tên tệp tin.\r\n");
    }
    else if (strcasecmp(token, "QUIT") == 0) {
        send_response(client_sock, "221 Tạm biệt.\r\n");
        close(client_sock);
    }
    else {
        send_response(client_sock, "502 Lệnh không được hỗ trợ.\r\n");
    }
}

// Hàm xử lý tín hiệu SIGCHLD để tránh zombie processes
void sigchld_handler(int s) {
    // waitpid() sau khi fork
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// Hàm hiển thị menu bằng tiếng Việt
void display_menu() {
    printf("======================================\n");
    printf("       MÁY CHỦ FTP - MENU QUẢN LÝ     \n");
    printf("======================================\n");
    printf("1. Hiển thị tất cả tài khoản\n");
    printf("2. Thêm tài khoản mới\n");
    printf("3. Cập nhật tài khoản\n");
    printf("4. Xóa tài khoản\n");
    printf("5. Khởi động máy chủ\n");
    printf("6. Thoát\n");
    printf("======================================\n");
    printf("Chọn một tùy chọn (1-6): ");
}

// Hàm thêm tài khoản mới
void add_account(Account **head) {
    Account *new_acc = (Account*)malloc(sizeof(Account));
    printf("Nhập tên người dùng: ");
    fgets(new_acc->username, sizeof(new_acc->username), stdin);
    new_acc->username[strcspn(new_acc->username, "\n")] = '\0';

    printf("Nhập mật khẩu: ");
    fgets(new_acc->password, sizeof(new_acc->password), stdin);
    new_acc->password[strcspn(new_acc->password, "\n")] = '\0';

    printf("Nhập đường dẫn thư mục gốc: ");
    fgets(new_acc->root_dir, sizeof(new_acc->root_dir), stdin);
    new_acc->root_dir[strcspn(new_acc->root_dir, "\n")] = '\0';

    new_acc->next = NULL;

    // Thêm vào danh sách liên kết
    if (!(*head)) {
        *head = new_acc;
    } else {
        Account *current = *head;
        while (current->next)
            current = current->next;
        current->next = new_acc;
    }

    // Tạo thư mục nếu chưa tồn tại
    struct stat st = {0};
    if (stat(new_acc->root_dir, &st) == -1) {
        mkdir(new_acc->root_dir, 0700);
    }

    printf("Thêm tài khoản thành công!\n");
}

// Hàm hiển thị tất cả tài khoản
void show_accounts(Account *head) {
    printf("\nDanh sách tài khoản:\n");
    printf("---------------------------------------------------------\n");
    printf("| %-20s | %-20s | %-30s |\n", "Tên người dùng", "Mật khẩu", "Thư mục gốc");
    printf("---------------------------------------------------------\n");
    Account *current = head;
    while (current) {
        printf("| %-20s | %-20s | %-30s |\n", current->username, current->password, current->root_dir);
        current = current->next;
    }
    printf("---------------------------------------------------------\n\n");
}

// Hàm lưu danh sách tài khoản vào file
void save_accounts(const char *filename, Account *head) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Không thể mở file để lưu tài khoản");
        return;
    }

    Account *current = head;
    while (current) {
        fprintf(file, "%s,%s,%s\n", current->username, current->password, current->root_dir);
        current = current->next;
    }

    fclose(file);
    printf("Lưu danh sách tài khoản thành công!\n");
}

// Hàm tìm tài khoản theo tên người dùng
Account* find_account(Account *head, const char *username) {
    Account *current = head;
    while (current) {
        if (strcmp(current->username, username) == 0)
            return current;
        current = current->next;
    }
    return NULL;
}

// Hàm cập nhật tài khoản
void update_account(Account *head) {
    char username[50];
    printf("Nhập tên người dùng cần cập nhật: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';

    Account *acc = find_account(head, username);
    if (!acc) {
        printf("Không tìm thấy tài khoản.\n");
        return;
    }

    printf("Cập nhật mật khẩu (nhấn Enter để giữ nguyên): ");
    char password[50];
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = '\0';
    if (strlen(password) > 0)
        strcpy(acc->password, password);

    printf("Cập nhật đường dẫn thư mục gốc (nhấn Enter để giữ nguyên): ");
    char root_dir[256];
    fgets(root_dir, sizeof(root_dir), stdin);
    root_dir[strcspn(root_dir, "\n")] = '\0';
    if (strlen(root_dir) > 0) {
        strcpy(acc->root_dir, root_dir);
        // Tạo thư mục nếu chưa tồn tại
        struct stat st = {0};
        if (stat(acc->root_dir, &st) == -1) {
            mkdir(acc->root_dir, 0700);
        }
    }

    printf("Cập nhật tài khoản thành công!\n");
}

// Hàm xóa tài khoản
void delete_account(Account **head) {
    char username[50];
    printf("Nhập tên người dùng cần xóa: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';

    Account *current = *head;
    Account *prev = NULL;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            if (prev)
                prev->next = current->next;
            else
                *head = current->next;
            free(current);
            printf("Xóa tài khoản thành công!\n");
            return;
        }
        prev = current;
        current = current->next;
    }
    printf("Không tìm thấy tài khoản.\n");
}

// Hàm xử lý kết nối từ client
void handle_client(int client_sock, Account *accounts, struct sockaddr_in client_addr) {
    char buffer[BUFFER_SIZE];
    char current_dir[256] = "/";
    bool logged_in = false;
    Account *current_account = NULL;

    send_response(client_sock, "220 Chào mừng đến với Máy Chủ FTP của chúng tôi.\r\n");

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(client_sock, buffer, sizeof(buffer)-1, 0);
        if (bytes_received <= 0) {
            printf("Client %s đã ngắt kết nối.\n", inet_ntoa(client_addr.sin_addr));
            close(client_sock);
            exit(0);
        }

        buffer[bytes_received] = '\0';
        handle_command(client_sock, buffer, current_dir, client_addr);
    }
}

int main() {
    Account *accounts = load_accounts(ACCOUNT_FILE);
    if (!accounts) {
        fprintf(stderr, "Không thể tải tài khoản.\n");
        exit(1);
    }

    // Thiết lập xử lý tín hiệu SIGCHLD để tránh zombie processes
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // Xử lý tín hiệu SIGCHLD
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Tạo socket
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size;

    if ((server_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Không thể tạo socket");
        exit(1);
    }

    // Cấu hình địa chỉ
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Lắng nghe trên tất cả các interface
    memset(&(server_addr.sin_zero), '\0', 8);

    // Bind
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("Bind lỗi");
        exit(1);
    }

    // Listen
    if (listen(server_sock, MAX_CLIENTS) == -1) {
        perror("Listen lỗi");
        exit(1);
    }

    printf("Máy Chủ FTP đang chạy trên cổng %d...\n", PORT);

    // Menu quản lý
    int choice;
    while (1) {
        display_menu();
        scanf("%d", &choice);
        getchar(); // Xóa ký tự newline còn lại trong buffer

        switch (choice) {
            case 1:
                show_accounts(accounts);
                break;
            case 2:
                add_account(&accounts);
                save_accounts(ACCOUNT_FILE, accounts);
                break;
            case 3:
                update_account(accounts);
                save_accounts(ACCOUNT_FILE, accounts);
                break;
            case 4:
                delete_account(&accounts);
                save_accounts(ACCOUNT_FILE, accounts);
                break;
            case 5: {
                printf("Khởi động Máy Chủ FTP...\n");
                // Chấp nhận kết nối và xử lý client
                while (1) {
                    sin_size = sizeof(struct sockaddr_in);
                    if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
                        perror("Accept lỗi");
                        continue;
                    }

                    printf("Kết nối từ %s\n", inet_ntoa(client_addr.sin_addr));

                    if (!fork()) { // Tiến trình con
                        close(server_sock);
                        handle_client(client_sock, accounts, client_addr);
                        exit(0);
                    }
                    close(client_sock);
                }
                break;
            }
            case 6:
                printf("Thoát chương trình.\n");
                // Giải phóng danh sách tài khoản
                while (accounts) {
                    Account *temp = accounts;
                    accounts = accounts->next;
                    free(temp);
                }
                close(server_sock);
                exit(0);
            default:
                printf("Lựa chọn không hợp lệ. Vui lòng chọn lại.\n");
        }
    }

    return 0;
}
