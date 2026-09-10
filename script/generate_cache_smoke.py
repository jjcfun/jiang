#!/usr/bin/env python3
"""用已验证的编译器检查生成缓存的跨进程、跨包和文件边界；所有产物写入 build。"""

import argparse
import fcntl
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--work-dir", default="build/generate-cache-smoke")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    compiler = Path(args.compiler).resolve()
    work = Path(args.work_dir).resolve()
    if not work.is_relative_to(repo / "build"):
        parser.error("--work-dir must be inside the repository build directory")
    work.mkdir(parents=True, exist_ok=False)
    for name in ("app", "tool", "helper", "logs"):
        (work / name).mkdir()
    (work / "app/package.ini").write_text("[package]\nname=input\nroot=main.jiang\n[dependencies]\ntools=../tool\n[generate.models]\npackage=tools\n")
    (work / "app/main.jiang").write_text("struct Input { Int value; }\n")
    (work / "tool/package.ini").write_text(
        "[package]\nname=generator\nroot=main.jiang\n[dependencies]\nhelper=../helper\n"
    )
    (work / "helper/package.ini").write_text("[package]\nname=helper\nroot=main.jiang\n")
    helper = work / "helper/main.jiang"
    helper.write_text('@life() public UInt8[]& revision() { return "one"; }\n')
    generator = work / "tool/main.jiang"
    generator.write_text('''#doc(module) 显式缓存只管理给定 key；payload 故意不纳入 key，以验证不透明内容的复用规则。
alias helper = import helper;
@entry(generate)
Void emit(reflect.Module root) {
    _ data = generate.read("payload").bytes();
    _ key = reflect.Fingerprint.of("opaque-unit");
    if (generate.cache_read("unit", key) is .some(bytes)) {
        generate.write("payload", bytes);
        generate.write("hit", "yes");
        generate.write("stored", "yes");
    } else {
        Bool stored = generate.cache_write("unit", key, data);
        generate.write("payload", data);
        generate.write("hit", "no");
        generate.write("stored", if stored { "yes" } else { "no" });
    }
    generate.write("revision", helper.revision());
}
''')
    payload = work / "tool/payload"
    payload.write_bytes(b"first\0complete")
    cache = work / "cache"
    results = []

    def command(output):
        return [str(compiler), "--artifact-cache-dir", str(cache), "generate", str(work / "app"),
                "--name", "models", "-o", str(output)]

    def verify(name, output, expected, hit, stored=True, revision="one"):
        assert (output / "payload").read_bytes() == expected, name
        assert (output / "hit").read_text() == ("yes" if hit else "no"), name
        assert (output / "stored").read_text() == ("yes" if stored else "no"), name
        assert (output / "revision").read_text() == revision, name
        results.append(name)

    def run(name, expected, hit, stored=True, revision="one"):
        output = work / "output"
        with (work / "logs" / f"{name}.log").open("wb") as log:
            result = subprocess.run(command(output), cwd=repo, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assert result.returncode == 0, f"{name}: see {log.name}"
        verify(name, output, expected, hit, stored, revision)

    run("cold", b"first\0complete", False)
    run("warm", b"first\0complete", True)
    files = list(cache.glob("*/content/*.cache"))
    assert len(files) == 1, files
    cached = files[0]
    payload.write_bytes(b"second")
    run("explicit-input-policy", b"first\0complete", True)
    cached.write_bytes(b"externally edited")
    run("opaque-file-edit", b"externally edited", True)
    cached.unlink()
    run("missing-file", b"second", False)
    cached.unlink()
    cached.mkdir()
    run("failed-write-fallback", b"second", False, stored=False)
    assert cached.is_dir()
    assert not list(cached.parent.glob("*.tmp.*"))
    cached.rmdir()

    # 同一 key 的独立 CLI 进程同时启动；持锁期间不能完成提交，释放后只保留一个完整文件。
    data = b"concurrent\0payload\n" * 1024
    payload.write_bytes(data)
    low = int(cached.name.split("_")[0])
    lock_path = cached.parent.parent / "locks" / f"content-{low & 63}.lock"
    workers = []

    def close_workers():
        for process, log, _ in workers:
            if process.poll() is None:
                process.kill()
                process.wait()
            log.close()

    with lock_path.open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            for index in range(3):
                output = work / f"concurrent-{index}"
                log = (work / "logs" / f"concurrent-{index}.log").open("wb")
                process = subprocess.Popen(command(output), cwd=repo, stdout=log, stderr=subprocess.STDOUT)
                workers.append((process, log, output))
            time.sleep(2)
            assert all(process.poll() is None for process, _, _ in workers)
            assert not cached.exists()
        except BaseException:
            close_workers()
            raise
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)
    try:
        for index, (process, log, output) in enumerate(workers):
            assert process.wait(timeout=180) == 0, f"concurrent-{index}: see {log.name}"
            hit = (output / "hit").read_text() == "yes"
            verify(f"concurrent-{index}", output, data, hit)
    finally:
        close_workers()
    assert cached.read_bytes() == data
    assert not list(cached.parent.glob("*.tmp.*"))
    assert len(list(cached.parent.iterdir())) == 1

    payload.write_bytes(b"new helper output")
    helper.write_text('@life() public UInt8[]& revision() { return "two"; }\n')
    run("helper-body-changed", b"new helper output", False, revision="two")
    run("helper-body-warm", b"new helper output", True, revision="two")
    with helper.open("a") as source:
        source.write("Int unused() { return 99; }\n")
    run("helper-unrelated", b"new helper output", True, revision="two")
    (work / "app/main.jiang").write_text("struct Input { Int value; Bool extra; }\nInt unused() { return 9; }\n")
    run("input-unqueried", b"new helper output", True, revision="two")
    record = {"compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(), "passed": results}
    (work / "verified.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record), flush=True)


if __name__ == "__main__":
    main()
