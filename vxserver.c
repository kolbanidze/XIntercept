#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <getopt.h>

#define MAX_BUF 4096

void usage(const char *progname) {
    fprintf(stderr, "Usage: %s -p <PORT> -s <SECRET_KEY_HEX> -o <PUBLIC_KEY_HEX>\n", progname);
    exit(1);
}

// Просто recv может и не вернуть все данные за один вызов
static ssize_t recv_all(int fd, unsigned char *buf, size_t buf_size) {
    size_t total = 0;
    while (total < buf_size) {
        ssize_t n = recv(fd, buf + total, buf_size - total, 0);
        if (n < 0) return -1;
        if (n == 0) break;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

int main(int argc, char *argv[]) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    int port = 0;
    unsigned char server_seckey[crypto_box_SECRETKEYBYTES];
    unsigned char server_pubkey[crypto_box_PUBLICKEYBYTES];
    int have_seckey = 0, have_pubkey = 0;

    int opt;
    while ((opt = getopt(argc, argv, "p:s:o:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 's':
                if (sodium_hex2bin(server_seckey, sizeof(server_seckey),
                                   optarg, strlen(optarg), NULL, NULL, NULL) != 0) {
                    fprintf(stderr, "Invalid secret key\n");
                    return 1;
                }
                have_seckey = 1;
                break;
            case 'o':
                if (sodium_hex2bin(server_pubkey, sizeof(server_pubkey),
                                   optarg, strlen(optarg), NULL, NULL, NULL) != 0) {
                    fprintf(stderr, "Invalid public key\n");
                    return 1;
                }
                have_pubkey = 1;
                break;
            default:
                usage(argv[0]);
        }
    }

    if (port <= 0 || port > 65535 || !have_seckey || !have_pubkey)
        usage(argv[0]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sock);
        return 1;
    }

    if (listen(sock, 1) < 0) {
        perror("Listen failed");
        close(sock);
        return 1;
    }

    fprintf(stderr, "Server listening on port %d...\n", port);

    int client_sock = accept(sock, NULL, NULL);
    if (client_sock < 0) {
        perror("Accept failed");
        close(sock);
        return 1;
    }

    unsigned char encrypted_msg[MAX_BUF];
    ssize_t encrypted_len = recv_all(client_sock, encrypted_msg, sizeof(encrypted_msg));
    if (encrypted_len <= 0) {
        fprintf(stderr, "Receive failed or empty message\n");
        close(client_sock);
        close(sock);
        return 1;
    }

    if ((size_t)encrypted_len < crypto_box_SEALBYTES) {
        fprintf(stderr, "Message too short to contain valid ciphertext\n");
        close(client_sock);
        close(sock);
        return 1;
    }

    size_t decrypted_len = (size_t)encrypted_len - crypto_box_SEALBYTES;
    unsigned char decrypted_msg[MAX_BUF];

    if (crypto_box_seal_open(decrypted_msg, encrypted_msg, (size_t)encrypted_len,
                             server_pubkey, server_seckey) != 0) {
        fprintf(stderr, "Failed to decrypt message\n");
        close(client_sock);
        close(sock);
        return 1;
    }

    fwrite(decrypted_msg, 1, decrypted_len, stdout);

    sodium_memzero(decrypted_msg, sizeof(decrypted_msg));
    sodium_memzero(server_seckey, sizeof(server_seckey));
    close(client_sock);
    close(sock);
    return 0;
}