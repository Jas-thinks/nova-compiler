# Single-container deployment: C++ compiler + FastAPI backend + built React frontend.
FROM node:20-slim AS frontend
WORKDIR /app/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
RUN npx vite build

FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY compiler/ compiler/
RUN rm -rf compiler/build compiler/build-san \
 && cmake -S compiler -B compiler/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build compiler/build -j2 --target novac
COPY backend/requirements.txt backend/requirements.txt
RUN pip install --no-cache-dir -r backend/requirements.txt
COPY backend/app backend/app
COPY examples/ examples/
COPY --from=frontend /app/frontend/dist frontend/dist
ENV NOVAC_BIN=/app/compiler/build/novac
WORKDIR /app/backend
CMD ["sh", "-c", "uvicorn app.main:app --host 0.0.0.0 --port ${PORT:-8000}"]
