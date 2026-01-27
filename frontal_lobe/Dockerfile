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

RUN pip3 install --no-cache-dir \
    --index-url https://download.pytorch.org/whl/rocm6.2 \
    --no-deps \
    torch torchvision torchaudio

RUN pip install --upgrade pip

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

RUN python -m spacy download en_core_web_sm


RUN pip3 install --no-cache-dir \
    torch torchvision torchaudio \
    --index-url https://download.pytorch.org/whl/cpu \
    --force-reinstall && \
    pip3 install "numpy<2.0" --force-reinstall

ARG HF_TOKEN

RUN python3 -c "from gliner import GLiNER; GLiNER.from_pretrained('urchade/gliner_medium-v2.1')"

COPY . .

RUN printf "#!/bin/bash\n\
cd /app\n\
python3 src/data_analysis/seismic_brain.py &\n\
python3 src/data_analysis/radio_news_brain.py" > /start_brains.sh

RUN chmod +x /start_brains.sh

CMD ["/start_brains.sh"]