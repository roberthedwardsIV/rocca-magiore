FROM python:3.10-slim
WORKDIR /app
RUN pip install fastapi uvicorn redis asyncpg websockets psycopg2-binary docker
COPY . .
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]