FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY backend/ ./backend/
COPY frontend/ ./frontend/

RUN mkdir -p /app/Data && touch /app/Data/users.txt

WORKDIR /app/backend
RUN g++ -std=c++17 -pthread main.cpp auth.cpp storage.cpp -o agap

EXPOSE 8080
CMD ["./agap"]
