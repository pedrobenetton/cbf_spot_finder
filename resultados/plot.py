import os
import re
import matplotlib.pyplot as plt
from statistics import mean

base_dir = "."   # root directory
node_dirs = ["02_nodes", "04_nodes", "06_nodes", "08_nodes"]
saida_filename = "saida"

header_regex = re.compile(
    r"np=(\d+),\s*OMP_NUM_THREADS=(\d+),\s*ppn=(\d+)"
)
time_regex = re.compile(
    r"Total execution time:\s*([0-9]+\.[0-9]+)"
)

results = {}

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
            content = f.read()

        blocks = content.strip().split("\n\n")

        for block in blocks:
            header = header_regex.search(block)
            tmatch = time_regex.search(block)

            if header and tmatch:
                np_value = int(header.group(1))
                exec_time = float(tmatch.group(1))

                if np_value not in results[node_count]:
                    results[node_count][np_value] = []

                results[node_count][np_value].append(exec_time)

plt.figure(figsize=(10, 6))

for nodes in sorted(results.keys()):
    np_values = sorted(results[nodes].keys())
    mean_times = [mean(results[nodes][npv]) for npv in np_values]

    plt.plot(np_values, mean_times, marker="o", label=f"{nodes} nodes")

plt.xlabel("np (number of MPI processes)")
plt.ylabel("Mean execution time (s)")
plt.title("Performance Scaling Across MPI Configurations")
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.show()

print("\nSummary of parsed results:\n")
for nodes in sorted(results.keys()):
    print(f"=== {nodes} nodes ===")
    for npv in sorted(results[nodes].keys()):
        times = results[nodes][npv]
        print(f"  np={npv}: {len(times)} runs, mean={mean(times):.6f}")
    print()
