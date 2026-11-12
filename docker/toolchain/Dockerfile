ARG BUILD_IMAGE_VERSION=latest
FROM ghcr.io/s0cks/kura/build:${BUILD_IMAGE_VERSION}

COPY scripts/compile-toolchain /usr/bin/compile-toolchain

WORKDIR /workspace
RUN chmod +x /usr/bin/compile-toolchain

CMD [ "/usr/bin/compile-toolchain" ]
