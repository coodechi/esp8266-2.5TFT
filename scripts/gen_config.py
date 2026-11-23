"""编译时读取 .env，把配置传给编译器。不写第二份配置文件。"""

from pathlib import Path

ENV_NAME = ".env"

KEYS = (
    "WIFI_SSID",
    "WIFI_PASS",
    "CITY_NAME",
    "WEATHER_CITY",
    "BAMBU_HOST",
    "BAMBU_SERIAL",
    "BAMBU_CODE",
)


def load_env(path):
    values = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values


def c_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')


def apply(env):
    env_path = Path(env["PROJECT_DIR"]) / ENV_NAME
    if not env_path.is_file():
        raise SystemExit("没有 .env。先复制：cp .env.example .env")
    values = load_env(env_path)
    missing = [key for key in KEYS if key not in values]
    if missing:
        raise SystemExit(".env 缺少 " + ", ".join(missing))
    env.Append(CPPDEFINES=[(key, '\\"%s\\"' % c_string(values[key])) for key in KEYS])


try:
    Import("env")
except Exception:
    env = None

if env is not None:
    apply(env)
elif __name__ == "__main__":
    raise SystemExit("配置只在编译时从 .env 读取。请运行 make build 或 make upload。")
