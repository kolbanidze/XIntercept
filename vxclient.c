#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <getopt.h>

void usage(const char *progname) {
    fprintf(stderr, "Usage: %s -i <IP> -p <PORT> -o <PUBLIC_KEY_HEX> -m <ENCRYPTION_KEY_HEX>\n", progname);
    exit(1);
}

int main(int argc, char *argv[]) {
    char *ip = NULL;
    int port = 0;
    int verbose = 0;
    unsigned char *message = NULL;
    unsigned char server_pubkey[crypto_box_PUBLICKEYBYTES] = { 0 };

    int opt;
    while ((opt = getopt(argc, argv, "i:p:o:m:v")) != -1) {
        switch (opt) {
            case 'v':
                verbose = 1;
                break;
            case 'i':
                ip = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'o':
                if (sodium_hex2bin(server_pubkey, sizeof(server_pubkey), optarg, strlen(optarg), NULL, NULL, NULL) != 0) {
                    if (verbose) fprintf(stderr, "Invalid public key\n");
                    return 1;
                }
                break;
            case 'm':
                message = optarg;
                break;
            default:
                if (verbose) usage(argv[0]);
        }
    }

    if (argc == 1) {
        if (verbose) fprintf(stderr, "No arguments provided.\n");
    }
    
    if (sodium_init() < 0) {
        if (verbose) fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    if (port <= 0) {
        if (verbose) usage(argv[0]);
        return 1;
    }
    if (!ip) {
        if (verbose) usage(argv[0]);
        return 1;
    }
    if (!message) {
        if (verbose) usage(argv[0]);
        return 1;
    }
    size_t msg_len = strlen(message);
    
    if (verbose) printf("%s\n", message);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        if (verbose) perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        if (verbose) fprintf(stderr, "Error: Invalid IP address format.\n");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        if (verbose) perror("Connection to server failed");
        close(sock);
        return -1;
    }
    unsigned char ciphertext[msg_len + crypto_box_SEALBYTES];

    if (crypto_box_seal(ciphertext, message, msg_len, server_pubkey) != 0) {
        if (verbose) fprintf(stderr, "Failed to encrypt!\n");
        return 1;
    }

    if (send(sock, ciphertext, sizeof(ciphertext), 0) < 0) {
        if (verbose) perror("Sending ciphertext failed");
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}
