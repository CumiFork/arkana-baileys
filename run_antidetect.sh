#!/data/data/com.termux/files/usr/bin/bash
# Run as root: su -c './run_antidetect.sh'
PY=/data/data/com.termux/files/usr/bin/python3
DIR=/data/data/com.termux/files/home/arkana-baileys
cd "$DIR" || exit 1
exec "$PY" ff_antidetect.py "$@"
