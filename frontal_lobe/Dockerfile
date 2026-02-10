FROM python:3.10.18-slim

RUN apt-get update && apt-get install -y \
    libpq-dev \
    build-essential \
    glpk-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

RUN pip install --upgrade pip

COPY requirements.txt .

RUN pip install --no-cache-dir -r requirements.txt

RUN python -m spacy download en_core_web_sm

COPY . .

ENV PYTHONUNBUFFERED=1

RUN printf "#!/bin/bash\n\
cd /app\n\
python3 src/data_analysis/seismic_brain.py &\n\
python3 src/data_analysis/radio_news_brain.py &\n\
python3 src/data_analysis/aviation_brain.py &\n\
python3 src/utils/aviation_baselines.py &\n\
python3 src/data_analysis/twitter_recon_brain.py &\n\
python3 src/trainers/asset_network_solver.py &\n\
python3 src/trainers/maritime_trainer.py &\n\
python3 src/data_analysis/maritime_brain.py &\n\
python3 src/data_analysis/financial_parser.py" > /start_brains.sh

RUN chmod +x /start_brains.sh

CMD ["/start_brains.sh"]