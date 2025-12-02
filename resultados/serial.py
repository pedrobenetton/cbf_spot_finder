import os
import re
import matplotlib.pyplot as plt

BASE_DIR = "."

cpu_dirs = [
    "serial_02_cpus",
    "serial_04_cpus",
    "serial_08_cpus",
    "serial_16_cpus",
    "serial_32_cpus",
    "serial_64_cpus"]
    #"serial_128_cpus",]

time_regex = re.compile(r"Total execution time:\s*([0-9.]+)")

data = {}

for dir_name in cpu_dirs:
    cpu_count = int(dir_name.replace("serial_", "").replace("_cpus", ""))
    full_dir = os.path.join(BASE_DIR, dir_name)

    data[cpu_count] = {}

    for filename in os.listdir(full_dir):
        if not filename.endswith(".txt"):
            continue

        mpi_procs = int(filename.replace(".txt", ""))

        with open(os.path.join(full_dir, filename)) as f:
            content = f.read()

        match = time_regex.search(content)
        if match:
            exec_time = float(match.group(1))
            data[cpu_count][mpi_procs] = exec_time
        else:
            print(f"Warning: No time found in {filename} inside {dir_name}")

all_mpi_procs = sorted({mpi for cpu in data for mpi in data[cpu]})

plot_cpus = False
plot_mpi = True

if plot_cpus:
	plt.figure(figsize=(10, 6))
	for mpi in all_mpi_procs:
	    x = []
	    y = []
	    for cpu in sorted(data.keys()):
	        if mpi in data[cpu]:
	            x.append(cpu)
	            y.append(data[cpu][mpi])
	    if x:
	        plt.plot(x, y, marker="o", label=f"{mpi} MPI processes")

	plt.xlabel("CPU count")
	plt.ylabel("Execution time (seconds)")
	plt.title("Execution time vs CPU count")
	plt.legend()
	plt.grid(True)
	plt.tight_layout()
	plt.savefig("exec_time_vs_cpu.png")
	plt.show()

if plot_mpi:
	plt.figure(figsize=(10, 6))
	for cpu in sorted(data.keys()):
	    x = []
	    y = []
	    for mpi in all_mpi_procs:
	        if mpi in data[cpu]:
	            x.append(mpi)
	            y.append(data[cpu][mpi])
	    if x:
	        plt.plot(x, y, marker="o", label=f"{cpu} CPUs")
	
	plt.xlabel("MPI process count")
	plt.ylabel("Execution time (seconds)")
	plt.title("Execution time vs MPI process count")
	plt.legend()
	plt.grid(True)
	plt.tight_layout()
	plt.savefig("exec_time_vs_mpi.png")
	plt.show()

