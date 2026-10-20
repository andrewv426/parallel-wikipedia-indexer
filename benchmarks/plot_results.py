"""Render the saved indexing measurements; does not run the indexer."""

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import numpy as np


ROOT = Path(__file__).resolve().parent
DATA = ROOT / "data"
FIGURES = ROOT / "figures"
BLUE = "#2364aa"
TEAL = "#147d79"
ORANGE = "#c06826"


def rows(filename):
    with (DATA / filename).open(newline="", encoding="utf-8-sig") as file:
        return list(csv.DictReader(file))


def style(axis):
    axis.spines[["top", "right"]].set_visible(False)
    axis.grid(axis="y", color="#e5eaf0", linewidth=0.8)
    axis.set_axisbelow(True)
    axis.tick_params(length=0, pad=8)


def save(figure, name):
    figure.savefig(FIGURES / name, dpi=180, facecolor="white")
    plt.close(figure)


def main():
    FIGURES.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "font.size": 11,
        "text.color": "#203047",
        "axes.labelcolor": "#33465e",
        "axes.edgecolor": "#c7d0dc",
        "xtick.color": "#4a5b70",
        "ytick.color": "#4a5b70",
        "axes.titleweight": "bold",
        "axes.titlepad": 18,
    })
    summary = rows("summary.csv")
    threads = sorted(
        (row for row in summary if row["category"] == "thread_sweep"),
        key=lambda row: int(row["n_threads"]),
    )
    x = np.array([int(row["n_threads"]) for row in threads])
    y = np.array([float(row["mbps"]) for row in threads])
    figure, axis = plt.subplots(figsize=(9, 4.8), layout="constrained")
    style(axis)
    axis.plot(x, y, color=BLUE, linewidth=2.5, marker="o", markersize=7)
    axis.fill_between(x, y, color=BLUE, alpha=0.07)
    for worker_count, throughput in zip(x, y):
        axis.annotate(f"{throughput:,.0f}", (worker_count, throughput),
                      xytext=(0, 11), textcoords="offset points", ha="center", fontsize=10)
    axis.set(title="Thread scaling on an 8.64 GB input", xlabel="Search workers",
             ylabel="Throughput (MiB/s)", xticks=x, ylim=(0, 2350), xlim=(0.2, 16.8))
    axis.text(0.02, 0.94, f"{y[-1] / y[0]:.2f}× speedup with 16 workers",
              transform=axis.transAxes, va="top", fontweight="bold", color=BLUE)
    save(figure, "thread_scaling.png")

    hashes = sorted(
        (row for row in summary if row["category"] == "hash_sweep"),
        key=lambda row: int(row["log2_hash_bins"]),
    )
    exponents = [int(row["log2_hash_bins"]) for row in hashes]
    values = [float(row["mbps"]) for row in hashes]
    figure, axis = plt.subplots(figsize=(9, 4.8), layout="constrained")
    style(axis)
    bars = axis.bar(range(len(exponents)), values, width=0.58,
                    color=[BLUE, BLUE, TEAL, BLUE])
    axis.bar_label(bars, labels=[f"{value:,.0f}" for value in values], padding=7)
    axis.set(title="Hash table sizing with 16 workers", xlabel="Hash bins per worker",
             ylabel="Throughput (MiB/s)", xticks=range(len(exponents)),
             xticklabels=[f"$2^{{{power}}}$" for power in exponents], ylim=(0, 2450))
    axis.text(2, 150, "DEFAULT", ha="center", color="white", fontsize=9, fontweight="bold")
    save(figure, "hash_sizing.png")

    lengths = rows("word_lengths.csv")
    length = np.array([int(row["length"]) for row in lengths])
    unique = np.array([int(row["unique_words"]) for row in lengths])
    occurrences = np.array([int(row["occurrences"]) for row in lengths])
    figure, axes = plt.subplots(1, 2, figsize=(12, 4.4), layout="constrained")
    figure.suptitle("Word lengths across the 30.24 GB input", fontweight="bold", fontsize=14)
    for axis, values, color, label in zip(
        axes, (unique, occurrences), (BLUE, TEAL),
        ("Unique words (millions)", "Occurrences (millions)"),
    ):
        style(axis)
        axis.bar(length, values / 1e6, color=color, width=0.78)
        axis.set(xlabel="Word length (characters)", ylabel=label, xlim=(2, 32),
                 xticks=[3, 7, 11, 15, 19, 23, 27, 31])
        axis.set_ylim(0, values.max() / 1e6 * 1.28)
        peak = int(np.argmax(values))
        axis.annotate(f"Peak: {length[peak]} characters", (length[peak], values[peak] / 1e6),
                      xytext=(6, 15), textcoords="offset points", fontsize=10, color=color)
        axis.yaxis.set_major_locator(MaxNLocator(5))
    save(figure, "word_lengths.png")

    frequencies = rows("word_frequencies.csv")
    rank = np.array([int(row["rank"]) for row in frequencies])
    count = np.array([int(row["count"]) for row in frequencies])
    slope, intercept = np.polyfit(np.log(rank), np.log(count), 1)
    fitted_log = intercept + slope * np.log(rank)
    r_squared = 1 - np.sum((np.log(count) - fitted_log) ** 2) / np.sum((np.log(count) - np.log(count).mean()) ** 2)
    figure, axis = plt.subplots(figsize=(9, 4.8), layout="constrained")
    style(axis)
    axis.loglog(rank, count, color=BLUE, linewidth=2, label="Measured counts")
    axis.loglog(rank, np.exp(fitted_log), color=ORANGE, linewidth=1.8,
                 linestyle="--", label=f"Power fit: exponent {slope:.3f}")
    axis.set(title="Rank-frequency distribution of the top 10,000 words",
             xlabel="Word rank (log scale)", ylabel="Occurrences (log scale)")
    axis.legend(frameon=False, loc="upper right")
    axis.text(0.03, 0.07, f"Log-space fit: R² = {r_squared:.4f}", transform=axis.transAxes, fontsize=10)
    save(figure, "word_frequencies.png")
    print(f"16-worker speedup: {y[-1] / y[0]:.6f}×")
    print(f"Rank-frequency exponent: {slope:.6f}; log-space R²: {r_squared:.6f}")


if __name__ == "__main__":
    main()
