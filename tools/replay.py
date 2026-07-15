"""Replay release fixtures and optionally check a second build for behavioral parity."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from report import render_review

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    "healthy": (0, "PROMOTE", []),
    "incomplete": (10, "HOLD", ["unpaired_windows", "low_candidate_volume"]),
    "regression": (20, "ROLLBACK", ["candidate_error_limit", "candidate_slow_limit",
                                    "error_regression", "latency_regression"]),
    "invalid": (2, None, []),
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(binary, source):
    return subprocess.run([binary, ROOT / "policy/release.conf", source],
                          capture_output=True, timeout=30)


def checked(result, name):
    expected_code, decision, reasons = CASES[name]
    if result.returncode != expected_code:
        raise ValueError(f"{name}: expected exit {expected_code}, got {result.returncode}. "
                         f"{result.stderr.decode(errors='replace')}")
    if decision is None:
        if result.stdout or b"duplicate window" not in result.stderr:
            raise ValueError("invalid fixture must be rejected without a decision")
        return None
    data = json.loads(result.stdout)
    routes = {r["route"]: r for r in data["routes"]}
    if (data["schema_version"] != 1 or data["decision"] != decision
            or sorted(routes) != ["browse", "checkout"]
            or routes["browse"]["decision"] != "PROMOTE"
            or routes["checkout"]["decision"] != decision
            or routes["checkout"]["reasons"] != reasons):
        raise ValueError(f"{name}: unexpected report: {data}")
    if name == "regression":
        checkout = routes["checkout"]
        if (checkout["candidate"]["errors"], checkout["candidate"]["requests"],
                checkout["candidate"]["error_bps"], checkout["candidate"]["slow_bps"]) != (30, 2000, 150, 800):
            raise ValueError("checkout regression metrics changed")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--compare", type=Path, help="compare stdout, stderr and exit code on each fixture")
    parser.add_argument("--out", type=Path, default=Path("reports/replay"))
    args = parser.parse_args()
    binaries = [args.binary.resolve()]
    if args.compare:
        binaries.append(args.compare.resolve())
    policy = ROOT / "policy/release.conf"
    manifest = {"policy_sha256": sha256(policy), "binary_sha256": [sha256(b) for b in binaries],
                "cases": {}}
    reports = {}
    for name in CASES:
        source = ROOT / "examples" / f"{name}.csv"
        result = execute(binaries[0], source)
        data = checked(result, name)
        if len(binaries) == 2:
            other = execute(binaries[1], source)
            if (result.returncode, result.stdout, result.stderr) != (other.returncode, other.stdout, other.stderr):
                raise ValueError(f"{name}: build outputs differ")
        if data is not None:
            reports[name] = data
        manifest["cases"][name] = {"input_sha256": sha256(source), "exit_code": result.returncode,
                                   "stdout_sha256": hashlib.sha256(result.stdout).hexdigest()}
        print(f"PASS {name}: exit {result.returncode}")
    args.out.mkdir(exist_ok=True, parents=True)
    for name, data in reports.items():
        (args.out / f"{name}.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (args.out / "release-review.html").write_text(render_review(reports), encoding="utf-8")


if __name__ == "__main__":
    main()
