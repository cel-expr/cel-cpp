# This Dockerfile is used to create a container around gcc9 and bazel for
# building the CEL C++ library on GitHub.
#
# To update a new version of this container, use gcloud. You may need to run
# `gcloud auth login` and `gcloud auth configure-docker` first.
#
# Note, if you need to run docker using `sudo` use the following commands
# instead:
#
#     sudo gcloud auth login --no-launch-browser
#     sudo gcloud auth configure-docker
#
# Run the following command from the root of the CEL repository:
#
#     gcloud builds submit --region=us -t gcr.io/cel-analysis/cel-cpp/ubuntu_floor .
#
# Once complete get the sha256 digest from the output using the following
# command:
#
#     gcloud artifacts versions list --package=cel-cpp/ubuntu_floor --repository=gcr.io \
#       --location=us
#
# The cloudbuild.yaml file must be updated to use the new digest like so:
#
#     - name: 'gcr.io/cel-analysis/cel-cpp/ubuntu_floor@<SHA256>'
FROM gcr.io/cloud-marketplace/google/ubuntu2204:latest

# Install Bazel prerequesites and required tools.
# See https://docs.bazel.build/versions/master/install-ubuntu.html
RUN apt-get update && apt-get upgrade -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
      git \
      libssl-dev \
      make \
      pkg-config \
      python3 \
      unzip \
      wget \
      zip \
      zlib1g-dev \
      default-jdk-headless \
      clang-12 \
      gcc-10 g++-10 \
      tzdata \
      && apt-get clean

# Install Bazelisk.
# https://github.com/bazelbuild/bazelisk/releases
ARG BAZELISK_URL="https://github.com/bazelbuild/bazelisk/releases/download/v1.27.0/bazelisk-amd64.deb"
ARG BAZELISK_CHKSUM="d8b00ea975c823e15263c80200ac42979e17368547fbff4ab177af035badfa83"
ADD ${BAZELISK_URL} /tmp/bazelisk.deb

ENV BAZELISK_CHKSUM=${BAZELISK_CHKSUM}
RUN echo "${BAZELISK_CHKSUM} */tmp/bazelisk.deb" | sha256sum --check

RUN apt-get install /tmp/bazelisk.deb

RUN mkdir -p /workspace
RUN mkdir -p /bazel

RUN USE_BAZEL_VERSION=8.7.0 bazelisk help
RUN USE_BAZEL_VERSION=7.3.2 bazelisk help

ENV CC=gcc-10
ENV CXX=g++-10

ENTRYPOINT ["/usr/bin/bazelisk"]
