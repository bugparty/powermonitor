// jetson_freq_reader.c - Kernel module to read CPU/GPU/EMC frequencies
// Provides /proc/jetson_freqs for single-read access to all frequencies
//
// Build:
//   make
//
// Load:
//   sudo insmod jetson_freq_reader.ko
//
// Read:
//   cat /proc/jetson_freqs
//   Output: cpu0_hz cpu4_hz gpu_hz emc_hz read_time_ns
//   Example: 1728000 1728000 1020000000 1600000000 1234
//
// Unload:
//   sudo rmmod jetson_freq_reader

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/cpufreq.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

// Jetson Nano specific paths
#define GPU_FREQ_PATH "/sys/devices/platform/bus@0/17000000.gpu/devfreq/17000000.gpu/cur_freq"
#define EMC_FREQ_PATH "/sys/class/devfreq/17000000.emc/cur_freq"

// Simple kernel-space file reader
static int read_sysfs_u64(const char *path, u64 *value)
{
    struct file *f;
    char buf[64];
    loff_t pos = 0;
    ssize_t len;

    f = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(f))
        return PTR_ERR(f);

    len = kernel_read(f, buf, sizeof(buf) - 1, &pos);
    filp_close(f, NULL);

    if (len <= 0)
        return -EIO;

    buf[len] = '\0';
    if (kstrtoull(buf, 10, value))
        return -EINVAL;

    return 0;
}

static int jetson_freq_show(struct seq_file *m, void *v)
{
    unsigned int cpu0_freq = 0, cpu4_freq = 0;
    u64 gpu_freq = 0, emc_freq = 0;
    struct cpufreq_policy *policy;
    u64 ts_start, ts_end;

    // Timestamp start
    ts_start = ktime_get_ns();

    // Read CPU cluster 0 (CPUs 0-3)
    policy = cpufreq_cpu_get(0);
    if (policy) {
        cpu0_freq = policy->cur;
        cpufreq_cpu_put(policy);
    }

    // Read CPU cluster 1 (CPUs 4-5) - Jetson Nano has 2 clusters
    policy = cpufreq_cpu_get(4);
    if (policy) {
        cpu4_freq = policy->cur;
        cpufreq_cpu_put(policy);
    } else {
        // If no cluster 1, use cluster 0 value (single-cluster system)
        cpu4_freq = cpu0_freq;
    }

    // Read GPU frequency from sysfs
    read_sysfs_u64(GPU_FREQ_PATH, &gpu_freq);

    // Read EMC frequency (memory controller)
    read_sysfs_u64(EMC_FREQ_PATH, &emc_freq);

    // Timestamp end
    ts_end = ktime_get_ns();

    // Output format: cpu0_hz cpu4_hz gpu_hz emc_hz read_time_ns
    seq_printf(m, "%u %u %llu %llu %llu\n",
               cpu0_freq, cpu4_freq, gpu_freq, emc_freq, ts_end - ts_start);

    return 0;
}

static int jetson_freq_open(struct inode *inode, struct file *file)
{
    return single_open(file, jetson_freq_show, NULL);
}

static const struct proc_ops jetson_freq_proc_ops = {
    .proc_open = jetson_freq_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init jetson_freq_init(void)
{
    struct proc_dir_entry *entry;

    // Create /proc/jetson_freqs
    entry = proc_create("jetson_freqs", 0444, NULL, &jetson_freq_proc_ops);
    if (!entry) {
        pr_err("jetson_freq: failed to create /proc/jetson_freqs\n");
        return -ENOMEM;
    }

    pr_info("jetson_freq: module loaded, /proc/jetson_freqs created\n");
    return 0;
}

static void __exit jetson_freq_exit(void)
{
    remove_proc_entry("jetson_freqs", NULL);
    pr_info("jetson_freq: module unloaded\n");
}

module_init(jetson_freq_init);
module_exit(jetson_freq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BOXR Research");
MODULE_DESCRIPTION("Jetson frequency reader via procfs");
MODULE_VERSION("1.0");
