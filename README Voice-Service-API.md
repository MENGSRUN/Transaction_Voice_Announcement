# Voice Service (FastAPI)

A minimal, production-friendly voice/TTS microservice designed to be called by **Spring Boot Core**.

This document is for developers who maintain, deploy, and operate the Voice Service. If you are integrating another service with this API, use [API.md](API.md).

## Features

- Create voice jobs (`POST /v1/voice/jobs`)
- Query job status (`GET /v1/voice/jobs/{job_id}`)
- Runtime metrics (`GET /v1/voice/metrics`)
- Prometheus scrape metrics (`GET /metrics`)
- Optional callback to Spring when audio is ready (best effort; callback failure does not invalidate ready audio)
- Basic Auth request verification for simple service-to-service auth
- Edge TTS provider for real speech generation
- ffmpeg post-processing to create ESP32-safe MP3 output
- Local file storage for generated audio
- Redis audio cache for repeated text/voice/speed/pitch/format combinations
- Optional idempotent job creation by `alert_id` for safe Transaction service retries
- Redis-backed job store + Redis Streams queue + queue backpressure + background worker concurrency

## Quickstart

1. Create a virtualenv

```bash
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

1. Install deps:

   ```bash
   pip install -r requirements.txt
   ```

1. Run API:

   ```bash
   uvicorn app.main:app --reload --port 9090
   ```

   For load testing/production process model (no `--reload`):

   ```bash
   uvicorn app.main:app --host 0.0.0.0 --port 9090 --workers 4
   ```

1. Run Redis (docker):

   ```bash
   docker compose up -d
   ```

1. Run worker:

   ```bash
   python -m app.worker
   ```

## Hear Audio Locally (Khmer test)

1. In `.env`, set:

   ```bash
   STORAGE_BASE_URL=http://127.0.0.1:9090/media
   LOCAL_AUDIO_DIR=./data/audio
   TTS_PROVIDER=edge_tts
   ```

1. Restart API + worker after changing env.
1. Run test script:

   ```bash
   python tests/enqueue_test_job.py
   ```

1. When status is `ready`, open the printed `audio_url` in your browser.

Tips for clearer Khmer pronunciation:

- Use `format: "mp3"` (the service supports mp3 output).
- Supported voices are only:
  - `km-KH-SreymomNeural`
  - `km-KH-PisethNeural`
- Use Khmer script directly (not Latin transliteration).
- Use short sentence chunks with punctuation (`។`, `,`, `!`) to control pauses.
- Use slower speed (`0.85` to `0.95`).
- Use small pitch changes first (`-20` to `20` Hz); extreme values can sound artificial.
- For mixed English text, the service transliterates names to Khmer-style pronunciation and spells short codes/acronyms.

## Throughput Controls

- `MAX_QUEUE_DEPTH`: reject new create-job requests with HTTP `429` when queue is too deep.
- `VOICE_JOB_DAILY_QUOTA_PER_IP`: max accepted `POST /v1/voice/jobs` requests per client IP per UTC day. Default: `1000`. Set `0` to disable.
- `WORKER_CONCURRENCY`: number of worker consumers inside `python -m app.worker`.
- `VOICE_STREAM_KEY`: Redis Stream used for queued voice jobs. Default: `voice:jobs:stream`.
- `VOICE_CONSUMER_GROUP`: Redis Stream consumer group used by workers. Default: `voice-workers`.
- `WORKER_STREAM_BLOCK_MS`: worker stream read block timeout. Default: `5000`.
- `WORKER_RECLAIM_IDLE_MS`: pending stream message idle time before another worker may reclaim it. Default: `60000`.
- `WORKER_MAX_ATTEMPTS`: maximum stream delivery attempts before a stuck job is marked failed. Default: `3`.
- `JOB_TTL_SECONDS`: Redis TTL for job records to prevent unbounded memory growth.
- `AUDIO_CACHE_TTL_SECONDS`: Redis TTL for completed audio cache entries.
- `FFMPEG_BIN`: ffmpeg executable path used for MP3 post-processing.
- `OUTPUT_MP3_SAMPLE_RATE`: generated MP3 sample rate. Default: `22050`.
- `OUTPUT_MP3_CHANNELS`: generated MP3 channels. Default: `1` for mono.
- `OUTPUT_MP3_BITRATE`: generated MP3 bitrate. Default: `32k`.
- `TTS_RETRY_ATTEMPTS`: retry count for transient TTS/network failures.
- `CALLBACK_RETRY_ATTEMPTS`: retry count for Spring callback delivery.

## ESP32 Audio Output

Generated MP3 files are converted with ffmpeg before storage so LOLIN32 Lite / ESP32 devices without PSRAM can download and play them from a reusable RAM buffer.

Default output profile:

```text
Sample rate: 22050 Hz
Channels: mono
Bitrate: 32 kbps
Loudness: normalized during ffmpeg post-processing
```

Equivalent ffmpeg command:

```bash
ffmpeg -i input.mp3 -af loudnorm -ar 22050 -ac 1 -b:a 32k output.mp3
```

Install `ffmpeg` on the API/worker host and make sure it is available on `PATH`, or set `FFMPEG_BIN` to the full executable path.

`-af loudnorm` applies ffmpeg loudness normalization. This makes generated voice files more consistently loud before the ESP32 downloads them. Do not rely on ESP32 firmware gain to fix quiet source audio; pushing I2S gain too high can cause clipping and distortion.

ESP32 compatibility contract:

- `audio_url` must use HTTPS.
- Host must be `voice-service-api.weknowyouknow.dev`.
- Path must end with `.mp3`.
- The media endpoint must return HTTP `200` directly; the ESP32 firmware does not follow redirects.
- The media response must include `Content-Length`.
- MP3 file size must be greater than `0` and at most `64 KB`.
- URL length must be less than `256` characters.
- Keep announcements short. At `32 kbps`, `64 KB` is roughly 16 seconds of audio.

## Cache Behavior

The service normalizes text and caches completed audio by:

```text
sha256(voice + speed + pitch + format + output_audio_profile + normalized_text)
```

If the same announcement text is requested again with the same voice, speed, pitch, and format, the API returns a ready job that reuses the existing `audio_url` instead of generating a new MP3.

If callers provide `alert_id`, the service maps it to one job. If Transaction Voice Announcement retries the same `alert_id`, Voice Service returns the existing job instead of creating duplicate work. Public/test callers can omit `alert_id` and let the service generate an internal one.

## Environment

Copy `.env.example` to `.env` and fill values.

## Metrics

The service exposes two metrics endpoints:

- `GET /v1/voice/metrics`: authenticated JSON for humans and internal checks.
- `GET /metrics`: Prometheus text format for scrapers.

`/metrics` reads the Redis metrics hash and current queue depth at scrape time. Redis remains the source of truth for job, cache, retry, callback, and failure counters; the Prometheus endpoint does not maintain a second counter system.

Prometheus metrics currently exported:

```text
voice_queue_depth
voice_max_queue_depth
voice_queue_usage_ratio
voice_jobs_created_total
voice_jobs_started_total
voice_jobs_ready_total
voice_jobs_failed_total
voice_cache_hits_total
voice_cache_misses_total
voice_duplicate_alert_hits_total
voice_processing_seconds_total
voice_callbacks_success_total
voice_callbacks_failed_total
voice_tts_retries_total
voice_callback_retries_total
voice_tts_failures_total
voice_ffmpeg_failures_total
voice_storage_failures_total
voice_worker_heartbeats
voice_worker_errors_total
voice_worker_job_exceptions_total
voice_worker_invalid_messages_total
voice_worker_jobs_abandoned_total
voice_legacy_queue_jobs_migrated_total
voice_rate_limit_hits_total
```

Ruthless operations rule: do not expose `/metrics` publicly. Scrape it from a private network or put it behind infrastructure-level protection.

## Public Exposure Guardrails

Keep `POST /v1/voice/jobs` behind authentication and rate limits. TTS work is CPU/network-expensive, so a public endpoint without protection is an abuse target.

The application enforces Redis-backed limits after authentication:

```env
VOICE_JOB_DAILY_QUOTA_PER_IP=1000
```

When exceeded, the API returns:

```http
429 Too Many Requests
```

This protects the worker queue from authenticated overuse, but it should not be your only shield. Put WAF/rate limiting in front of FastAPI so junk traffic is rejected before it reaches the app.

The per-IP quota uses `CF-Connecting-IP` first, then the first `X-Forwarded-For` value, then the direct socket IP. Only trust this when FastAPI is behind trusted Cloudflare/Traefik routing. If the API is directly exposed, clients can spoof forwarding headers.

Recommended outer layers:

- Cloudflare WAF rule: challenge or block suspicious traffic to `/v1/voice/jobs`.
- Cloudflare rate limiting rule: limit `POST` requests to `/v1/voice/jobs` by source IP or client characteristics.
- Traefik middleware: optional local rate limit for the voice router.
- Firewall/private networking: keep Prometheus, Grafana, Redis, and `/metrics` off the public internet.

Practical starting policy:

```text
POST /v1/voice/jobs: 1000 requests/day per client IP
GET /v1/voice/jobs/{job_id}: allow more generously for polling
```

Do not rely on `alert_id` for abuse protection. It is an idempotency tool, not a security control.

## Service-to-service auth (Basic Auth)

Transaction Voice Announcement calls Voice Service with HTTP Basic Auth:

```bash
curl -u voice_service:change_me_voice_password \
  -H "Content-Type: application/json" \
  -X POST http://localhost:9090/v1/voice/jobs \
  -d '{"text":"អ្នកបានទទួលប្រាក់ ៥០០០ រៀល","voice":"km-KH-SreymomNeural","speed":0.9,"pitch":0,"format":"mp3"}'
```

Set these environment variables in Voice Service:

```bash
VOICE_AUTH_MODE=basic
VOICE_BASIC_USERNAME=voice_service
VOICE_BASIC_PASSWORD=<strong-password>
```

Use HTTPS or a private internal network. Basic Auth is only safe when transport is protected.

HMAC auth is still available by setting `VOICE_AUTH_MODE=hmac`.

## Docker Deploy

Build and run locally:

```bash
docker compose up -d --build
```

The compose file starts:

- `api`: FastAPI on `VOICE_API_PORT`, default `9090`
- `worker`: background TTS worker
- `redis`: Redis with append-only persistence
- `prometheus`: scrapes `api:9090/metrics`, exposed on `PROMETHEUS_PORT`, default `9091`
- `grafana`: dashboard UI exposed on `GRAFANA_PORT`, default `3000`

Open Grafana locally:

```text
http://127.0.0.1:3000
```

Default login comes from `.env`:

```env
GRAFANA_ADMIN_USER=admin
GRAFANA_ADMIN_PASSWORD=<strong-password>
```

The compose setup provisions:

- Prometheus datasource: `http://prometheus:9090`
- Dashboard: `Voice Service`

Ruthless deployment rule: never leave `GRAFANA_ADMIN_PASSWORD=admin` in production, and do not expose Grafana or Prometheus directly to the public internet.

For production, set `STORAGE_BASE_URL` to the URL that IoT devices can actually reach, for example:

```env
STORAGE_BASE_URL=https://voice-service-api.weknowyouknow.dev/media
```

Do not use `127.0.0.1` for IoT devices. On the device, `127.0.0.1` means the ESP32 itself, not your server.

## GitLab CI/CD

The pipeline in `.gitlab-ci.yml`:

1. Installs dependencies and runs Python compile checks.
1. Runs the pytest suite.
1. Builds a Docker image with ffmpeg included.
1. Pushes the image to the GitLab Container Registry.
1. Copies `docker-compose.yml` and `monitoring/` to the deploy server.
1. Provides a manual production deploy job over SSH using the pushed registry image.

Required GitLab CI/CD variables for deploy:

```text
DEPLOY_HOST              server IP or hostname
DEPLOY_USER              SSH user on the server
DEPLOY_PATH              directory containing docker-compose.yml on the server
DEPLOY_SSH_PRIVATE_KEY   private key allowed to SSH into the server
```

On the deploy server:

```bash
mkdir -p /opt/voice-service-api
cd /opt/voice-service-api
```

Place these files there:

```text
docker-compose.yml
.env
```

Your production `.env` must include real secrets:

```env
VOICE_AUTH_MODE=basic
VOICE_BASIC_USERNAME=<service-username>
VOICE_BASIC_PASSWORD=<strong-password>
REDIS_URL=redis://redis:6379/0
STORAGE_BASE_URL=https://voice-service-api.weknowyouknow.dev/media
LOCAL_AUDIO_DIR=/app/data/audio
FFMPEG_BIN=ffmpeg
OUTPUT_MP3_SAMPLE_RATE=22050
OUTPUT_MP3_CHANNELS=1
OUTPUT_MP3_BITRATE=32k
```

Ruthless deployment rule: keep `.env`, generated MP3 files, logs, virtualenvs, and `__pycache__` out of Git. They are machine state, not source code.
