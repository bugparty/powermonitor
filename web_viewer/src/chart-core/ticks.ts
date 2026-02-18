import type { TimeTicks } from "../types";

export function buildTimeTicks(start_us: number, end_us: number, max_major_ticks = 8, minor_divisions = 10): TimeTicks {
    const span = Math.max(1, end_us - start_us);
    const target_major_step = span / max_major_ticks;
    const nice_steps_us = [
        10_000, 20_000, 50_000,
        100_000, 200_000, 500_000,
        1_000_000, 2_000_000, 5_000_000,
        10_000_000, 20_000_000, 30_000_000, 60_000_000
    ];

    let major_step_us = nice_steps_us[nice_steps_us.length - 1];
    for (const candidate of nice_steps_us) {
        if (candidate >= target_major_step) {
            major_step_us = candidate;
            break;
        }
    }

    const minor_step_us = Math.max(1_000, Math.floor(major_step_us / minor_divisions));
    const major_ticks: number[] = [];
    const minor_ticks: number[] = [];

    const major_start = Math.floor(start_us / major_step_us) * major_step_us;
    for (let time_us = major_start; time_us <= end_us; time_us += major_step_us) {
        if (time_us >= start_us) {
            major_ticks.push(time_us);
        }
    }

    const minor_start = Math.floor(start_us / minor_step_us) * minor_step_us;
    for (let time_us = minor_start; time_us <= end_us; time_us += minor_step_us) {
        if (time_us >= start_us && time_us % major_step_us !== 0) {
            minor_ticks.push(time_us);
        }
    }

    return { majorTicks: major_ticks, minorTicks: minor_ticks, majorStepUs: major_step_us, minorStepUs: minor_step_us };
}
