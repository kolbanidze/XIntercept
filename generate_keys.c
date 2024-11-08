#include <sodium.h>
#include <stdio.h>

int main() {
    if (sodium_init() < 0) {
        printf("libsodium initialization failed\n");
        return 1;
    }

    unsigned char pubkey[crypto_box_PUBLICKEYBYTES];
    unsigned char seckey[crypto_box_SECRETKEYBYTES];
    crypto_box_keypair(pubkey, seckey);

    printf("Public Key: ");
    for (int i = 0; i < crypto_box_PUBLICKEYBYTES; i++) printf("%02x", pubkey[i]);
    printf("\nSecret Key: ");
    for (int i = 0; i < crypto_box_SECRETKEYBYTES; i++) printf("%02x", seckey[i]);
    printf("\n");

    return 0;
}
