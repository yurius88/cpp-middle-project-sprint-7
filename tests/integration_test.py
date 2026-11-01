"""Интеграционный тест прокси-сервера: python (origin) + wget (клиент).

Поднимает origin-заглушку (tests/origin_stub.py), запускает AsyncHttpProxy
и скачивает страницы через прокси утилитой wget. Проверяет:
 * передачу тела с Content-Length (тело больше буфера чтения прокси);
 * пустое тело (Content-Length: 0);
 * одновременную обработку нескольких сессий.

Запуск: python3 tests/integration_test.py <путь к AsyncHttpProxy>
"""

from __future__ import annotations

import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

SKIP_RETURN_CODE = 77
CONCURRENCY = 4
WGET_TIMEOUT = 15.0


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_port(port: int, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"порт {port} не открылся за {timeout} с")


def wget(url: str, output: Path, proxy_port: int) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            "wget",
            "--timeout=10",
            "--tries=1",
            "-e", "use_proxy=yes",
            "-e", f"http_proxy=127.0.0.1:{proxy_port}",
            "-O", str(output),
            url,
        ],
        capture_output=True,
        text=True,
        timeout=WGET_TIMEOUT,
    )


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <path-to-AsyncHttpProxy>", file=sys.stderr)
        return 2
    if shutil.which("wget") is None:
        print("SKIP: утилита wget не найдена", file=sys.stderr)
        return SKIP_RETURN_CODE

    proxy_bin = Path(sys.argv[1]).resolve()
    if not proxy_bin.is_file():
        print(f"FAIL: бинарник {proxy_bin} не найден (сначала соберите проект)", file=sys.stderr)
        return 1

    origin_port = free_port()
    proxy_port = free_port()

    failures: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        origin_log = tmp_path / "origin.log"
        proxy_log = tmp_path / "proxy.log"

        with origin_log.open("w", encoding="utf-8") as origin_out, proxy_log.open("w", encoding="utf-8") as proxy_out:
            origin = subprocess.Popen(
                [sys.executable, str(Path(__file__).resolve().parent / "origin_stub.py"), str(origin_port)],
                stdout=origin_out,
                stderr=subprocess.STDOUT,
            )
            proxy = subprocess.Popen(
                [str(proxy_bin), str(proxy_port)],
                stdout=proxy_out,
                stderr=subprocess.STDOUT,
            )
            try:
                wait_for_port(origin_port)
                wait_for_port(proxy_port)
                base = f"http://127.0.0.1:{origin_port}"

                # 1. Тело с Content-Length, больше буфера чтения прокси.
                out = tmp_path / "plain.txt"
                result = wget(f"{base}/plain", out, proxy_port)
                if result.returncode != 0:
                    failures.append(f"plain: wget завершился с кодом {result.returncode}: {result.stderr.strip()}")
                elif out.read_bytes() != b"A" * 4096:
                    failures.append(f"plain: получено {out.stat().st_size} байт, ожидалось 4096")

                # 2. Пустое тело (Content-Length: 0).
                out = tmp_path / "empty.txt"
                result = wget(f"{base}/empty", out, proxy_port)
                if result.returncode != 0:
                    failures.append(f"empty: wget завершился с кодом {result.returncode}: {result.stderr.strip()}")
                elif out.stat().st_size != 0:
                    failures.append(f"empty: получено {out.stat().st_size} байт, ожидалось 0")

                # 3. Несколько сессий одновременно.
                concurrent: dict[int, tuple[int, int, str]] = {}
                lock = threading.Lock()

                def worker(idx: int) -> None:
                    out_i = tmp_path / f"concurrent{idx}.txt"
                    try:
                        res = wget(f"{base}/plain", out_i, proxy_port)
                        size = out_i.stat().st_size if res.returncode == 0 else -1
                        err = res.stderr.strip()
                    except subprocess.TimeoutExpired:
                        res, size, err = None, -1, "wget превысил таймаут"
                    with lock:
                        concurrent[idx] = (res.returncode if res else -1, size, err)

                threads = [threading.Thread(target=worker, args=(i,)) for i in range(CONCURRENCY)]
                for thread in threads:
                    thread.start()
                for thread in threads:
                    thread.join(timeout=WGET_TIMEOUT + 5)

                for idx in range(CONCURRENCY):
                    if idx not in concurrent:
                        failures.append(f"concurrent[{idx}]: запрос не завершился")
                        continue
                    code, size, err = concurrent[idx]
                    if code != 0:
                        failures.append(f"concurrent[{idx}]: wget завершился с кодом {code}: {err}")
                    elif size != 4096:
                        failures.append(f"concurrent[{idx}]: получено {size} байт, ожидалось 4096")

                if proxy.poll() is not None:
                    failures.append(f"прокси завершился досрочно с кодом {proxy.returncode}")
            except Exception as exc:  # noqa: BLE001
                failures.append(f"ошибка стенда: {exc}")
            finally:
                for process in (proxy, origin):
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()

        if failures:
            print("ИНТЕГРАЦИОННЫЙ ТЕСТ ПРОВАЛЕН:", file=sys.stderr)
            for failure in failures:
                print(f"  - {failure}", file=sys.stderr)
            print("--- лог прокси ---", file=sys.stderr)
            print(proxy_log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            print("--- лог origin ---", file=sys.stderr)
            print(origin_log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            return 1

    print("Интеграционный тест пройден: plain, empty, concurrent x4")
    return 0


if __name__ == "__main__":
    sys.exit(main())
