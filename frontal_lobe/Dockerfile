FROM python:3.10.18-slim

RUN apt-get update && apt-get install -y \
    libpq-dev \
    build-essential \
    gfortran \
    libopenblas-dev \
    liblapack-dev \
    libxml2-dev \
    libxslt-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Run torch installation here manually to avoid downloading bloated Nvidia drivers
RUN pip3 install --no-cache-dir \
    --index-url https://download.pytorch.org/whl/rocm6.2 \
    --no-deps \
    torch torchvision torchaudio
    
RUN pip install --upgrade pip

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

RUN python -m spacy download en_core_web_sm

COPY . .

CMD bash -c "python3 src/data_analysis/seismic_brain.py"