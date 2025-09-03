FROM python:3.11-buster

RUN pip install poetry

ENV POETRY_NO_INTERACTION=1 \
    POETRY_VIRTUALENVS_IN_PROJECT=1 \
    POETRY_VIRTUALENVS_CREATE=1 \
    POETRY_CACHE_DIR=/tmp/poetry_cache

WORKDIR /app

COPY pyproject.toml poetry.lock ./
COPY src ./src
RUN touch README.md

RUN poetry install && rm -rf $POETRY_CACHE_DIR

ENTRYPOINT ["poetry", "run", "fastapi", "run", "src/server/server.py", "--port", "80"]