FROM alpine:latest

RUN apk add --no-cache gcc musl-dev libsodium-dev libsodium-static make

WORKDIR /build
COPY generate_keys.c .
COPY vxclient.c .
COPY vxserver.c .
COPY Makefile .

RUN make clean && make

CMD ["sh"]
