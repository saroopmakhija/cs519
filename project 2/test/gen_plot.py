import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import seaborn as sns
import numpy as np
import os

mfolders = ["mmapr", "fmapr"]

def read_latency_data():
    buf_sizes = [65536, 262144, 1048576, 4194304, 16777216, 67108864, 268435456]
    threads = [1, 2, 4, 8, 16, 32, 64]

    all_est_latency_data = []
    all_st_latency_data = []

    for mf in mfolders:
        for buf in buf_sizes:
            for th in threads:
                path = os.path.join(mf, str(buf), str(th))

                est_file = os.path.join(path, "est.csv")
                st_file = os.path.join(path, "st.csv")

                if os.path.exists(est_file):
                    est_df = pd.read_csv(est_file, sep=" ", header=None, names=["thread", "timestamp", "cycles"], dtype={"thread": int, "timestamp": int, "cycles": int})
                    est_df["mfolder"] = mf
                    est_df["buf_size"] = buf
                    est_df["thread_count"] = th
                    est_df = est_df.sort_values(by=["thread", "timestamp"])
                    est_df["time_diff"] = est_df.groupby("thread")["timestamp"].diff()
                    all_est_latency_data.append(est_df)

                if os.path.exists(st_file):
                    st_df = pd.read_csv(st_file, sep=" ", header=None, names=["thread", "timestamp", "cycles"], dtype={"thread": int, "timestamp": int, "cycles": int})
                    st_df["mfolder"] = mf
                    st_df["buf_size"] = buf
                    st_df["thread_count"] = th
                    st_df = st_df.sort_values(by=["thread", "timestamp"])
                    st_df["time_diff"] = st_df.groupby("thread")["timestamp"].diff()
                    all_st_latency_data.append(st_df)

    est_combined = pd.concat(all_est_latency_data, ignore_index=True) if all_est_latency_data else pd.DataFrame(columns=["thread", "timestamp", "cycles", "mfolder", "buf_size", "thread_count", "time_diff"])
    st_combined = pd.concat(all_st_latency_data, ignore_index=True) if all_st_latency_data else pd.DataFrame(columns=["thread", "timestamp", "cycles", "mfolder", "buf_size", "thread_count", "time_diff"])

    return est_combined, st_combined

def plot_3d_latency_per_type(df, label):
    for mfolder in mfolders:
        sub = df[df["mfolder"] == mfolder].copy()
        stats = sub.groupby(["thread", "thread_count", "buf_size"])["time_diff"].mean().reset_index()

        stats = stats.dropna()
        stats["thread"] = stats["thread"] + 1
        stats = stats.astype({"thread": int, "thread_count": int, "time_diff": float, "buf_size": int})

        fig = plt.figure(figsize=(12, 8))
        ax = fig.add_subplot(111, projection='3d')

        colors = plt.cm.viridis_r(np.linspace(0, 1, len(stats["buf_size"].unique())))

        for i, buf in enumerate(sorted(stats["buf_size"].unique())):
            buf_stats = stats[stats["buf_size"] == buf]
            buf_stats = buf_stats.copy()
            buf_stats["time_diff_norm"] = (buf_stats["time_diff"] / buf_stats["time_diff"].max())
            ax.plot_trisurf(
                buf_stats['thread'],
                buf_stats['thread_count'],
                buf_stats['time_diff_norm'],
                color=colors[i],
                edgecolor='none',
                alpha=0.8,
                label=f"Bufsize {buf}"
            )

        ax.set_title(f"{label} Latency Surface: Thread vs ThreadCount vs TimeDiff ({mfolder})")
        ax.set_xlabel("Thread")
        ax.invert_xaxis()
        ax.set_ylabel("Thread Count")
        ax.set_yticks(range(0, 65, 8))
        ax.set_zlabel("Mean Time Difference (%)")
        
        legend_labels = [f"Bufsize {buf}" for buf in sorted(stats["buf_size"].unique())]
        legend_patches = [plt.Line2D([0], [0], marker='o', color='w', label=lbl,
                                     markerfacecolor=colors[i], markersize=8) for i, lbl in enumerate(legend_labels)]
        ax.legend(handles=legend_patches)
        
        plt.tight_layout()
        filename = f"out/3d_surface_latency_{label.lower()}_{mfolder}.png"
        plt.savefig(filename)
        plt.close()

def plot_mean_norm_surface(est_df, st_df):
    est_df["source"] = "Extents"
    st_df["source"] = "Normal"
    for mfolder in mfolders:
        combined = pd.concat([est_df, st_df], ignore_index=True)
        combined = combined[combined["mfolder"] == mfolder]

        combined = combined.dropna(subset=["time_diff"])
        combined["time_diff_norm"] = combined.groupby(["buf_size", "thread_count", "source"])['time_diff'].transform(lambda x: x / x.max())

        stats = combined.groupby(["thread_count", "buf_size", "source"])['time_diff_norm'].mean().reset_index()
        buf_set = sorted(stats["buf_size"].unique())
        buf_map = {b: i * 4 for i, b in enumerate(buf_set)}
        stats["bufs_stretched"] = stats["buf_size"].map(buf_map)

        fig = plt.figure(figsize=(14, 10))
        ax = fig.add_subplot(111, projection='3d')

        markers = {"Extents": 'o', "Normal": '^'}
        colors = {"Extents": 'green', "Normal": 'teal'}

        for source in stats["source"].unique():
            sub = stats[stats["source"] == source]
            ax.plot_trisurf(
                sub['thread_count'],
                sub['bufs_stretched'],
                sub['time_diff_norm'],
                # color=colors[source],
                edgecolor='none',
                alpha=0.5,
                label=source
            )

        ax.set_title("Mean Normalized Time Diff: Thread Count vs Buffer Size")
        ax.set_ylabel("Buffer Size")
        ax.set_yticks(list(buf_map.values()))
        ax.set_yticklabels(list(buf_map.keys()), rotation=30, verticalalignment='center')
        ax.set_xlabel("Thread Count")
        ax.set_xticks(range(0, 65, 8))
        ax.invert_xaxis()
        ax.invert_zaxis()
        ax.set_zlabel("Mean Normalized Time Diff")
        ax.legend()
        plt.subplots_adjust(left=0.15, right=0.85, top=0.85, bottom=0.15)
        plt.tight_layout()
        plt.savefig(f"out/mean_norm_time_diff_surface_{mfolder}.png")
        plt.close()

if __name__ == "__main__":
    if not os.path.exists('out') or not os.path.isdir('out'):
        os.makedirs('out')

    est_df, st_df = read_latency_data()
    plot_3d_latency_per_type(est_df, "Extents")
    plot_3d_latency_per_type(st_df, "Normal")
    # plot_mean_norm_surface(est_df, st_df)
