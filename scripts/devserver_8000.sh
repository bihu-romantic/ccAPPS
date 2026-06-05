#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/home/c/ccAPPS"
CONDA_INIT="/home/c/miniconda3/etc/profile.d/conda.sh"
CONDA_ENV="pple_cc"
LOG_FILE="/tmp/ccapps-runserver-8000.log"
PID_FILE="/tmp/ccapps-runserver-8000.pid"
RUN_ARGS="runserver 0.0.0.0:8000"
if [[ "${CCAPPS_NORELOAD:-0}" == "1" ]]; then
  RUN_ARGS="runserver 0.0.0.0:8000 --noreload"
fi
PATTERN="ccAPPSctl.py ${RUN_ARGS}"

usage() {
  cat <<'EOF'
Usage:
  scripts/devserver_8000.sh start
  scripts/devserver_8000.sh stop
  scripts/devserver_8000.sh restart
  scripts/devserver_8000.sh status
  scripts/devserver_8000.sh probe

Notes:
  - Starts Django via: python ccAPPSctl.py runserver 0.0.0.0:8000
  - Set CCAPPS_NORELOAD=1 to disable auto-reload
  - Log file: /tmp/ccapps-runserver-8000.log
EOF
}

is_listening() {
  ss -ltnp | grep -qE "0.0.0.0:8000|127.0.0.1:8000"
}

status() {
  echo "== Process =="
  ps -ef | grep -nE "ccAPPSctl.py runserver 0.0.0.0:8000|ccAPPSctl.py runserver 127.0.0.1:8000" || true
  echo
  echo "== Port 8000 =="
  ss -ltnp | grep -nE ":8000" || true
  echo
  echo "== Probe /data/login =="
  curl -s -o /dev/null -w "HTTP %{http_code}\n" "http://127.0.0.1:8000/data/login/" || true
  echo
  echo "== Log tail =="
  tail -n 20 "$LOG_FILE" 2>/dev/null || true
}

start() {
  if is_listening; then
    echo "Port 8000 already has a listener. Run 'status' or 'restart'."
    status
    return 0
  fi

  : > "$LOG_FILE"
  (
    source "$CONDA_INIT"
    conda activate "$CONDA_ENV"
    cd "$APP_DIR"
    nohup python ccAPPSctl.py $RUN_ARGS >>"$LOG_FILE" 2>&1 &
    echo $! > "$PID_FILE"
  )

  sleep 2
  if is_listening; then
    echo "Started on 0.0.0.0:8000"
    status
  else
    echo "Start failed. Check log:"
    tail -n 80 "$LOG_FILE" || true
    return 1
  fi
}

stop() {
  pkill -f "ccAPPSctl.py runserver 0.0.0.0:8000" || true
  pkill -f "ccAPPSctl.py runserver 127.0.0.1:8000" || true
  sleep 1
  if is_listening; then
    echo "Port 8000 still in use:"
    ss -ltnp | grep -nE ":8000" || true
    return 1
  fi
  rm -f "$PID_FILE"
  echo "Stopped."
}

probe() {
  curl -i -s "http://127.0.0.1:8000/data/login/" | sed -n '1,20p'
}

cmd="${1:-}"
case "$cmd" in
  start) start ;;
  stop) stop ;;
  restart) stop; start ;;
  status) status ;;
  probe) probe ;;
  *) usage; exit 1 ;;
esac
