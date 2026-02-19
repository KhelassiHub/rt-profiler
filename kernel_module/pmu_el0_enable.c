#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rt-profiler");
MODULE_DESCRIPTION("Enable direct EL0 PMU cycle counter access on AArch64");

#if !defined(__aarch64__)
#error "This module is intended for AArch64 only"
#endif

static void enable_pmu_on_cpu(void *info)
{
	u64 val;

	asm volatile("mrs %0, pmuserenr_el0" : "=r"(val));
	val |= 0xfULL;
	asm volatile("msr pmuserenr_el0, %0" :: "r"(val));

	asm volatile("mrs %0, pmcntenset_el0" : "=r"(val));
	val |= (1ULL << 31);
	asm volatile("msr pmcntenset_el0, %0" :: "r"(val));

	asm volatile("mrs %0, pmcr_el0" : "=r"(val));
	val |= (1ULL << 0);
	asm volatile("msr pmcr_el0, %0" :: "r"(val));

	asm volatile("isb");
}

static void disable_pmu_on_cpu(void *info)
{
	u64 val;

	asm volatile("mrs %0, pmuserenr_el0" : "=r"(val));
	val &= ~0xfULL;
	asm volatile("msr pmuserenr_el0, %0" :: "r"(val));

	asm volatile("mrs %0, pmcntenclr_el0" : "=r"(val));
	val |= (1ULL << 31);
	asm volatile("msr pmcntenclr_el0, %0" :: "r"(val));

	asm volatile("isb");
}

static int __init pmu_el0_enable_init(void)
{
	on_each_cpu(enable_pmu_on_cpu, NULL, 1);
	pr_info("pmu_el0_enable: enabled EL0 PMU cycle counter access on all CPUs\n");
	return 0;
}

static void __exit pmu_el0_enable_exit(void)
{
	on_each_cpu(disable_pmu_on_cpu, NULL, 1);
	pr_info("pmu_el0_enable: disabled EL0 PMU cycle counter access\n");
}

module_init(pmu_el0_enable_init);
module_exit(pmu_el0_enable_exit);
