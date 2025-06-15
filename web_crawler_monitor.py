import subprocess
import time
import os
import numpy as np

def get_memory_usage_mb(pid):
    """Reads memory usage from /proc/<pid>/status (Linux only)"""
    try:
        with open(f'/proc/{pid}/status', 'r') as f:
            for line in f:
                if 'VmRSS' in line:
                    return int(line.split()[1]) / 1024  # kB to MB
    except FileNotFoundError:
        return 0
    return 0

def monitor_program(command):
    print("Starting program...")
    start_time = time.time()

    # Launch the C program
    process = subprocess.Popen(command)
    pid = process.pid

    cpu_samples = []
    mem_samples = []
    time_stamps = []

    while process.poll() is None:
        now = time.time()
        elapsed = now - start_time

        # Read memory and fake CPU usage sample (not accurate but placeholder)
        mem = get_memory_usage_mb(pid)

        # Store samples
        time_stamps.append(elapsed)
        cpu_samples.append(0)  # Placeholder since we avoid psutil
        mem_samples.append(mem)

        time.sleep(0.1)

    end_time = time.time()
    duration = end_time - start_time

    # Convert to numpy arrays
    cpu_np = np.array(cpu_samples)
    mem_np = np.array(mem_samples)

    print("\n📊 Statistics Summary (measured using NumPy only)")
    print("--------------------------------------------------")
    print(f"✅ Execution Time      : {duration:.2f} seconds")
    print(f"💾 Avg Memory Usage    : {np.mean(mem_np):.2f} MB")
    print(f"📦 Peak Memory Usage   : {np.max(mem_np):.2f} MB")
    print(f"📉 Std Dev (Memory)    : {np.std(mem_np):.2f} MB")
    print(f"🧪 Samples Collected   : {len(mem_np)}")

if __name__ == "__main__":
    command = ["./r.out", "url.txt", "4", "100"]
    monitor_program(command)
