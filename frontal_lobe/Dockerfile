FROM python:3.10.18-slim

# Basic C++ tools for compilation
RUN apt-get update && apt-get install -y \
    libpq-dev \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

RUN pip install --upgrade pip

COPY requirements.txt .
# This installs TensorFlow (for Seismic) and Spacy (for Radio). 
# It will be MUCH faster without Torch/GLiNER.
RUN pip install --no-cache-dir -r requirements.txt

# Lightweight Spacy English Model (12MB)
RUN python -m spacy download en_core_web_sm

COPY . .

ENV PYTHONUNBUFFERED=1

# Startup Script
RUN printf "#!/bin/bash\n\
cd /app\n\
python3 src/data_analysis/seismic_brain.py &\n\
python3 src/data_analysis/radio_news_brain.py &\n\
python3 src/data_analysis/twitter_recon_brain.py" > /start_brains.sh

RUN chmod +x /start_brains.sh

CMD ["/start_brains.sh"]