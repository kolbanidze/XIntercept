#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <getopt.h>

void usage(const char *progname) {
    fprintf(stderr, "Usage: %s -p <PORT> -s <SECRET_KEY_HEX> -o <PUBLIC_KEY> > message.txt\n", progname);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    int port = 0;
    unsigned char server_seckey[crypto_box_SECRETKEYBYTES];
    unsigned char server_pubkey[crypto_box_PUBLICKEYBYTES];

    int opt;
    while ((opt = getopt(argc, argv, "p:s:o:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 's':
                if (sodium_hex2bin(server_seckey, sizeof(server_seckey), optarg, strlen(optarg), NULL, NULL, NULL) != 0) {
                    fprintf(stderr, "Invalid secret key\n");
                    return 1;
                }
                break;
            case 'o':
                if (sodium_hex2bin(server_pubkey, sizeof(server_pubkey), optarg, strlen(optarg), NULL, NULL, NULL) != 0) {
                    fprintf(stderr, "Invalid public key\n");
                    return 1;
                }
                break;
            default:
                usage(argv[0]);
        }
    }

    if (port <= 0) {
        usage(argv[0]);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(sock, 1) < 0) {
        perror("Listen failed");
        return 1;
    }

    printf("Server listening on port %d...\n", port);

    int client_sock = accept(sock, NULL, NULL);
    if (client_sock < 0) {
        perror("Accept failed");
        return 1;
    }

    unsigned char encrypted_msg[4096];
    ssize_t encrypted_len = recv(client_sock, encrypted_msg, sizeof(encrypted_msg), 0);
    if (encrypted_len <= 0) {
        perror("Receive failed");
        return 1;
    }
    unsigned char decrypted_msg[encrypted_len - crypto_box_SEALBYTES];
    if (crypto_box_seal_open(decrypted_msg, encrypted_msg, encrypted_len, server_pubkey, server_seckey) != 0) {
        fprintf(stderr, "Failed to decrypt message\n");
        return 1;
    }

    fwrite(decrypted_msg, 1, encrypted_len - crypto_box_SEALBYTES, stdout);
    close(client_sock);
    close(sock);
    return 0;
}
