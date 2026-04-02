#include <sodium.h>
#include <stdio.h>

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "libsodium initialization failed\n");
        return 1;
    }

    unsigned char pubkey[crypto_box_PUBLICKEYBYTES];
    unsigned char seckey[crypto_box_SECRETKEYBYTES];
    crypto_box_keypair(pubkey, seckey);

    // тут будут лежать hex строки секретных ключей (32 * 2 + 1) с учетом \0
    char pubhex[crypto_box_PUBLICKEYBYTES * 2 + 1];
    char sechex[crypto_box_SECRETKEYBYTES * 2 + 1];

    sodium_bin2hex(pubhex, sizeof(pubhex), pubkey, sizeof(pubkey));
    sodium_bin2hex(sechex, sizeof(sechex), seckey, sizeof(seckey));

    FILE *fp = fopen("keys.config", "w");
    if (fp == NULL) {
        fprintf(stderr, "Error: Could not open keys.config for writing\n");
        sodium_memzero(seckey, sizeof(seckey));
        return 1;
    }

    fprintf(fp, "%s\n", pubhex);
    fprintf(fp, "%s\n", sechex);

    fprintf(stdout, "Public Key: %s\n", pubhex);
    fprintf(stdout, "Secret Key: %s\n", sechex);

    fclose(fp);
    printf("[+] Keys successfully written to keys.config\n");

    sodium_memzero(seckey, sizeof(seckey));
    sodium_memzero(sechex, sizeof(sechex));

    return 0;
}