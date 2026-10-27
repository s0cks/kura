ARG UBUNTU_VERSION=26.04
FROM ubuntu:${UBUNTU_VERSION}
SHELL [ "/bin/bash", "-o", "-pipefail", "-c" ]

ARG MESON_VERSION=1.11.1
ARG CURL_VERSION=8.18.0-1ubuntu2.5
ARG PIP3_VERSION=25.1.1+dfsg-1ubuntu2
ARG PY3_VERSION=3.14.3-0ubuntu2
ARG NINJA_VERSION=1.13.2-1
ARG BUILD_ESSENTIAL_VERSION=12.12ubuntu2.26.04.2
ARG PKG_CONFIG_VERSION=2.5.1-4
ARG CA_CERTIFICATES_VERSION=20260601~26.04.1
ARG GIT_VERSION=1:2.53.0-1ubuntu1
ARG CMAKE_VERSION=4.2.3-2ubuntu2
ARG GTEST_VERSION=1.17.0-1build1
ARG RUMDL_VERSION=0.2.73
ARG GOLANGCI_LINT_VERSION=2.13.2
ARG YAMLLINT_VERSION=1.37.1-1
ARG VALE_VERSION=3.21.0
ARG CLANG_VERSION=1:22.1.2-1ubuntu1
ARG SOFTWARE_PROPERTIES_COMMON_VERSION=0.120
ARG IWYU_VERSION=8.23-1build1
ARG UBUNTU_PRO_CLIENT_VERSION=37.2ubuntu0.1
ARG HADOLINT_VERSION=2.15.1

ENV DEBIAN_FRONTEND=noninteractive

ENV MESON_VERSION=${MESON_VERSION}
ENV CURL_VERSION=${CURL_VERSION}
ENV PIP3_VERSION=${PIP3_VERSION}
ENV PY3_VERSION=${PY3_VERSION}
ENV NINJA_VERSION=${NINJA_VERSION}
ENV BUILD_ESSENTIAL_VERSION=${BUILD_ESSENTIAL_VERSION}
ENV PKG_CONFIG_VERSION=${PKG_CONFIG_VERSION}
ENV CA_CERTIFICATES_VERSION=${CA_CERTIFICATES_VERSION}
ENV GIT_VERSION=${GIT_VERSION}
ENV CMAKE_VERSION=${CMAKE_VERSION}
ENV GTEST_VERSION=${GTEST_VERSION}
ENV RUMDL_VERSION=${RUMDL_VERSION}
ENV GOLANGCI_LINT_VERSION=${GOLANGCI_LINT_VERSION}
ENV YAMLLINT_VERSION=${YAMLLINT_VERSION}
ENV VALE_VERSION=${VALE_VERSION}
ENV CLANG_VERSION=${CLANG_VERSION}
ENV SOFTWARE_PROPERTIES_COMMON_VERSION=${SOFTWARE_PROPERTIES_COMMON_VERSION}
ENV IWYU_VERSION=${IWYU_VERSION}
ENV UBUNTU_PRO_CLIENT_VERSION=${UBUNTU_PRO_CLIENT_VERSION}

RUN sed -i 's/Components: main/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      curl=${CURL_VERSION} \
      ninja-build=${NINJA_VERSION} \
      python3=${PY3_VERSION} \
      python3-pip=${PIP3_VERSION} \
      build-essential=${BUILD_ESSENTIAL_VERSION} \
      pkg-config=${PKG_CONFIG_VERSION} \
      ca-certificates=${CA_CERTIFICATES_VERSION} \
      git=${GIT_VERSION} \
      cmake=${CMAKE_VERSION} \
      libgtest-dev=${GTEST_VERSION} \
      yamllint=${YAMLLINT_VERSION} \
      llvm-22-dev=${CLANG_VERSION} \
      clang-22=${CLANG_VERSION} \
      libclang-22-dev=${CLANG_VERSION} \
      clang-tidy-22=${CLANG_VERSION} \
      iwyu=${IWYU_VERSION} \
 && rm -rf /var/lib/apt/lists/* \
 && pip3 install \
      --no-cache-dir \
      --break-system-packages \
      meson==${MESON_VERSION}

# ╭───────────────╮
# │ Install rumdl │
# ╰───────────────╯
ENV RUMDL_DOWNLOAD_URL=https://github.com/rvben/rumdl/releases/download/v${RUMDL_VERSION}/rumdl-v${RUMDL_VERSION}-x86_64-unknown-linux-gnu.tar.gz
RUN curl -LsSf ${RUMDL_DOWNLOAD_URL} | tar xzf - -C /usr/local/bin
# ──────────────────────────────────────────────────────────────────────

# ╭───────────────────────╮
# │ Install golangci-lint │
# ╰───────────────────────╯
ENV GOLANGCI_LINT_DOWNLOAD_URL=https://github.com/golangci/golangci-lint/releases/download/v${GOLANGCI_LINT_VERSION}/golangci-lint-${GOLANGCI_LINT_VERSION}-linux-amd64.tar.gz
RUN curl -LsSf "${GOLANGCI_LINT_DOWNLOAD_URL}" \
      -o golangci-lint-${GOLANGCI_LINT_VERSION}-linux-amd64.tar.gz \
 && tar xf golangci-lint-${GOLANGCI_LINT_VERSION}-linux-amd64.tar.gz \
 && mv golangci-lint-${GOLANGCI_LINT_VERSION}-linux-amd64/golangci-lint /usr/local/bin
# ──────────────────────────────────────────────────────────────────────

# ╭──────────────╮
# │ Install vale │
# ╰──────────────╯
ENV VALE_DOWNLOAD_URL=https://github.com/vale-cli/vale/releases/download/v${VALE_VERSION}/vale_${VALE_VERSION}_Linux_64-bit.tar.gz
RUN curl -LsSf "${VALE_DOWNLOAD_URL}" -o vale_${VALE_VERSION}_Linux_64-bit.tar.gz \
 && tar xf vale_${VALE_VERSION}_Linux_64-bit.tar.gz \
 && mv vale /usr/local/bin
# ──────────────────────────────────────────────────────────────────────

# ╭──────────────────╮
# │ Install hadolint │
# ╰──────────────────╯
ENV HADOLINT_DOWNLOAD_URL=https://github.com/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint-linux-x86_64
RUN curl -LsSf "${HADOLINT_DOWNLOAD_URL}" \
      -o hadolint \
 && mv hadolint /usr/local/bin
# ──────────────────────────────────────────────────────────────────────

RUN mkdir -p /workspace
WORKDIR /workspace
