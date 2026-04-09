import os
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D

mfolders = ["mmap", "fmap"]

def read_data():
    buf_sizes = [65536, 262144, 1048576, 4194304, 16777216, 67108864, 268435456]
    threads = [1, 2, 4, 8, 16, 32, 64]

    all_est_data = []
    all_st_data = []

    for mf in mfolders:
        for buf in buf_sizes:
            for th in threads:
                path = os.path.join(mf, str(buf), str(th))
                est_file = os.path.join(path, "est.csv")
                st_file = os.path.join(path, "st.csv")

                if os.path.exists(est_file):
                    est_df = pd.read_csv(est_file, header=None, names=[
                        "thread", "totaltime", "avgtime", "totalext", "totalpage", "avgext", "avgpage"])
                    est_df["mfolder"] = mf
                    est_df["buf_size"] = buf
                    if len(est_df) == 2: # added to check using subset of data
                        est_df = pd.concat([est_df]*500, ignore_index=True)
                    all_est_data.append(est_df)

                if os.path.exists(st_file):
                    st_df = pd.read_csv(st_file, header=None, names=[
                        "thread", "totaltime", "avgtime"])
                    st_df["mfolder"] = mf
                    st_df["buf_size"] = buf
                    if len(st_df) == 2: # added to check using subset of data
                        st_df = pd.concat([st_df]*500, ignore_index=True)
                    all_st_data.append(st_df)

    est_combined = pd.concat(all_est_data, ignore_index=True)
    st_combined = pd.concat(all_st_data, ignore_index=True)
    return est_combined, st_combined


def plot_3d_graphs(est_df, st_df):
    buf_groups = {
        "small": [65536, 262144, 1048576],
        "large": [4194304, 16777216, 67108864, 268435456]
    }

    for mf in mfolders:
        for group_name, buffers in buf_groups.items():
            fig = plt.figure()
            ax = fig.add_subplot(111, projection='3d')

            handles = []
            labels = []

            unique_threads = sorted(est_df["thread"].unique())
            thread_map = {t: i * 4 for i, t in enumerate(unique_threads)}
            for label, df, color in [("Extents", est_df, 'lightgreen'), ("Normal", st_df, 'teal')]:
                subset = df[(df["mfolder"] == mf) & (df["buf_size"].astype(int).isin(buffers))]
                subset = subset.copy()
                subset["buf_size"] = subset["buf_size"].astype(int)
                subset["thread"] = subset["thread"].astype(int)

                subset["thread_stretched"] = subset["thread"].map(thread_map)

                pivot = subset.pivot_table(index="buf_size", columns="thread_stretched", values="avgtime", aggfunc='mean')
                X, Y = np.meshgrid(pivot.columns, pivot.index)
                Z = pivot.values

                surf = ax.plot_surface(X, Y, Z, color=color, alpha=0.7)
                handles.append(Line2D([0], [0], color=color, lw=4))
                labels.append(label)

            for xval in thread_map.values():
                ax.plot([xval, xval], [Y.min(), Y.max()], [Z.min(), Z.min()], 'k--', linewidth=0.5)

            ax.set_ylabel("Buffer Size")
            ax.set_xlabel("Threads")
            ax.set_xticks(list(thread_map.values()))
            ax.set_xticklabels(list(thread_map.keys()))
            ax.set_zlabel("Avg Time (μs)")
            ax.set_title(f"{mf.upper()} - {group_name.capitalize()} Buffers")
            ax.legend(handles, labels, loc='best')
            plt.savefig(f"out/{mf}_{group_name}_avgtime_surface.png")
            plt.close()


def plot_extents_graph(est_df):
    buf_set = [4194304, 16777216, 67108864, 268435456]

    for mf in mfolders:
        fig = plt.figure()
        ax = fig.add_subplot(111, projection='3d')

        subset = est_df[(est_df["mfolder"] == mf) & (est_df["buf_size"].isin(buf_set))]
        subset = subset.copy()
        subset["buf_size"] = subset["buf_size"].astype(int)
        subset["thread"] = subset["thread"].astype(int)

        unique_threads = sorted(subset["thread"].unique())
        thread_map = {t: i * 4 for i, t in enumerate(unique_threads)}
        buf_map = {b: i * 4 for i, b in enumerate(buf_set)}
        subset["thread_stretched"] = subset["thread"].map(thread_map)
        subset["bufs_stretched"] = subset["buf_size"].map(buf_map)

        pivot = subset.pivot_table(index="bufs_stretched", columns="thread_stretched", values="avgext", aggfunc='mean')
        X, Y = np.meshgrid(pivot.columns, pivot.index)
        Z = pivot.values

        surf = ax.plot_surface(X, Y, Z, cmap='inferno', alpha=0.75)

        ax.set_ylabel("Buffer Size")
        ax.set_yticks(list(buf_map.values()))
        ax.set_yticklabels(list(buf_map.keys()))
        ax.set_xlabel("Threads")
        ax.set_xticks(list(thread_map.values()))
        ax.set_xticklabels(list(thread_map.keys()))
        ax.set_zlabel("Avg Extents")
        ax.set_title(f"{mf.upper()} - Selected Buffers - Extents")
        plt.savefig(f"out/{mf}_selected_extents_surface.png")
        plt.close()


def plot_cdf(est_df):
    buf_sizes = [4194304, 16777216, 67108864, 268435456]
    threads = sorted(est_df["thread"].unique())

    for buf in buf_sizes:
        plt.figure()
        for th in threads:
            subset = est_df[
                (est_df["mfolder"] == "mmap") &
                (est_df["buf_size"] == buf) &
                (est_df["thread"] == th)
            ]
            if subset.empty:
                continue

            data = np.sort(subset["avgext"].values)
            cdf = np.arange(1, len(data)+1) / len(data)
            plt.plot(data, cdf, label=str(th) + " threads")

        plt.xlabel("Total Extents")
        plt.ylabel("CDF")
        plt.title("CDF for mmap - Buffer Size " + str(buf))
        plt.legend()
        plt.grid(True)
        plt.savefig("out/cdf_mmap_buf_" + str(buf) + ".png")
        plt.close()

if __name__ == "__main__":
    if not os.path.exists('out') or not os.path.isdir('out'):
        os.makedirs('out')

    est_df, st_df = read_data()
    plot_3d_graphs(est_df, st_df)
    plot_extents_graph(est_df)
    plot_cdf(est_df)
