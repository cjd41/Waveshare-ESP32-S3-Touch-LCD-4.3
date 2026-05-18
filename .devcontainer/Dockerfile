FROM debian:bookworm

RUN apt-get update && apt-get install -y \
    python3 python3-pip python3-venv \
    git curl udev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py \
    -o get-platformio.py \
    && python3 get-platformio.py \
    && rm get-platformio.py

ENV PATH="/root/.platformio/penv/bin:/root/.local/bin:${PATH}"

RUN platformio platform install espressif32