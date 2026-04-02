#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <getopt.h>

#define MAX_BUF 4096

void usage(const char *progname) {
    fprintf(stderr, "Usage: %s -i <IP> -p <PORT> -o <PUBLIC_KEY_HEX> -m <MESSAGE>\n", progname);
    exit(1);
}

static ssize_t send_all(int fd, const unsigned char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, buf + total, len - total, 0);
        if (n < 0) return -1;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

int main(int argc, char *argv[]) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    char *ip = NULL;
    int port = 0;
    char *message = NULL;
    unsigned char server_pubkey[crypto_box_PUBLICKEYBYTES];
    int have_pubkey = 0;

    int opt;
    while ((opt = getopt(argc, argv, "i:p:o:m:")) != -1) {
        switch (opt) {
            case 'i':
                ip = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'o':
                if (sodium_hex2bin(server_pubkey, sizeof(server_pubkey),
                                   optarg, strlen(optarg), NULL, NULL, NULL) != 0) {
                    fprintf(stderr, "Invalid public key\n");
                    return 1;
                }
                have_pubkey = 1;
                break;
            case 'm':
                message = optarg;
                break;
            default:
                usage(argv[0]);
        }
    }

    if (port <= 0 || port > 65535 || !ip || !message || !have_pubkey)
        usage(argv[0]);

    size_t msg_len = strlen(message);
    if (msg_len == 0) {
        fprintf(stderr, "Empty message\n");
        return 1;
    }
    if (msg_len + crypto_box_SEALBYTES > MAX_BUF) {
        fprintf(stderr, "Message too long\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    unsigned char ciphertext[MAX_BUF];
    if (crypto_box_seal(ciphertext, (const unsigned char *)message,
                        msg_len, server_pubkey) != 0) {
        fprintf(stderr, "Encryption failed\n");
        close(sock);
        return 1;
    }

    size_t cipher_len = msg_len + crypto_box_SEALBYTES;
    if (send_all(sock, ciphertext, cipher_len) < 0) {
        perror("Send failed");
        close(sock);
        return 1;
    }

    sodium_memzero(ciphertext, sizeof(ciphertext));
    close(sock);
    return 0;
}