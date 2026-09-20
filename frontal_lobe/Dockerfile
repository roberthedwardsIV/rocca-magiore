# Frontal lobe — analysis & inference services

FROM python:3.10.18-slim

RUN apt-get update && apt-get install -y \
    libpq-dev \
    build-essential \
    glpk-utils \
    postgresql-client \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

RUN pip install --upgrade pip

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
RUN python -m spacy download en_core_web_sm

COPY . .

ENV PYTHONUNBUFFERED=1
ENV PYTHONPATH=/app/src

RUN printf '#!/bin/bash\n\
until pg_isready -h hippocampus -p 5432 -U "${DB_USER:-rocco_admin}"; do\n\
  echo "Waiting for Hippocampus..." && sleep 2\n\
done\n\
\n\
python3 src/utils/seed_cities.py\n\
python3 src/data_analysis/seismic_brain.py &\n\
python3 src/data_analysis/twitter_recon_brain.py &\n\
wait\n' > /start_brains.sh \
 && chmod +x /start_brains.sh

CMD ["/start_brains.sh"]
