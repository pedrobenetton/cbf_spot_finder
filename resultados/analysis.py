import os
import re
import matplotlib.pyplot as plt
from statistics import mean

# === Configuration ===
base_dir = "."   # Root directory containing 02_nodes, 04_nodes, etc.
node_dirs = ["02_nodes", "04_nodes", "06_nodes", "08_nodes"]
saida_filename = "saida"

# === Regex patterns ===
header_regex = re.compile(r"np=(\d+),\s*OMP_NUM_THREADS=(\d+),\s*ppn=(\d+)")
time_regex = re.compile(r"Total execution time:\s*([0-9]+\.[0-9]+)")

# === Data structure ===
results = {}

# === Parse data ===
for ndir in node_dirs:
    node_path = os.path.join(base_dir, ndir)
    if not os.path.isdir(node_path):
        print(f"Skipping missing directory: {ndir}")
        continue

    node_count = int(ndir.split("_")[0])
    results[node_count] = {}

    for run in sorted(os.listdir(node_path)):
        run_path = os.path.join(node_path, run)
        saida_path = os.path.join(run_path, saida_filename)

        if not os.path.isfile(saida_path):
            continue

        with open(saida_path, "r") as f:
            content = f.read().strip()

        blocks = content.split("\n\n")
        for block in blocks:
            header = header_regex.search(block)
            tmatch = time_regex.search(block)
            if header and tmatch:
                np_value = int(header.group(1))
                exec_time = float(tmatch.group(1))
                results[node_count].setdefault(np_value, []).append(exec_time)

# === Compute mean execution times ===
mean_times = {
    nodes: {npv: mean(times) for npv, times in npdict.items()}
    for nodes, npdict in results.items()
}

# === Plot 1: Execution time vs np ===
plt.figure(figsize=(8, 6))
for nodes in sorted(mean_times.keys()):
    np_values = sorted(mean_times[nodes].keys())
    times = [mean_times[nodes][npv] for npv in np_values]
    plt.plot(np_values, times, marker="o", label=f"{nodes} nodes")

plt.xlabel("np (number of MPI processes)")
plt.ylabel("Mean execution time (s)")
plt.title("Execution Time vs MPI Processes")
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.show()

# === Plot 2 & 3: Speedup and Efficiency (Strong Scaling) ===
for nodes in sorted(mean_times.keys()):
    np_values = sorted(mean_times[nodes].keys())
    base_np = min(np_values)
    T1 = mean_times[nodes][base_np]

    speedup = [T1 / mean_times[nodes][npv] for npv in np_values]
    efficiency = [s / (npv / base_np) for s, npv in zip(speedup, np_values)]

    plt.figure(figsize=(10, 4))

    # Speedup
    plt.subplot(1, 2, 1)
    plt.plot(np_values, speedup, marker="o", label=f"{nodes} nodes")
    plt.plot(np_values, [npv / base_np for npv in np_values], "k--", label="Ideal")
    plt.xlabel("np (number of MPI processes)")
    plt.ylabel("Speedup")
    plt.title(f"Speedup (Strong Scaling) — {nodes} nodes")
    plt.grid(True)
    plt.legend()

    # Efficiency
    plt.subplot(1, 2, 2)
    plt.plot(np_values, efficiency, marker="o", label=f"{nodes} nodes")
    plt.xlabel("np (number of MPI processes)")
    plt.ylabel("Efficiency")
    plt.title(f"Parallel Efficiency — {nodes} nodes")
    plt.grid(True)
    plt.legend()

    plt.tight_layout()
    plt.show()

    # Numeric summary
    print(f"\n=== {nodes} nodes ===")
    for npv, s, e in zip(np_values, speedup, efficiency):
        print(f"  np={npv}: speedup={s:.2f}, efficiency={e:.2f}")

# === Plot 4: Weak Scaling Efficiency ===
plt.figure(figsize=(8, 6))
for nodes in sorted(mean_times.keys()):
    np_values = sorted(mean_times[nodes].keys())
    base_np = min(np_values)
    T1 = mean_times[nodes][base_np]
    weak_eff = [T1 / mean_times[nodes][npv] for npv in np_values]

    plt.plot(np_values, weak_eff, marker="o", label=f"{nodes} nodes")

plt.xlabel("np (number of MPI processes)")
plt.ylabel("Weak scaling efficiency")
plt.title("Weak Scaling Efficiency (approx.)")
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.show()
