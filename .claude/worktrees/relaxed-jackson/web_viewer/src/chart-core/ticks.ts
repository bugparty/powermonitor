import type { TimeTicks } from "../types";

export function buildTimeTicks(startUs: number, endUs: number, maxMajorTicks = 8, minorDivisions = 10): TimeTicks {
    const span = Math.max(1, endUs - startUs);
    const targetMajorStep = span / maxMajorTicks;
    const niceStepsUs = [
        10_000, 20_000, 50_000,
        100_000, 200_000, 500_000,
        1_000_000, 2_000_000, 5_000_000,
        10_000_000, 20_000_000, 30_000_000, 60_000_000
    ];

    let majorStepUs = niceStepsUs[niceStepsUs.length - 1];
    for (const candidate of niceStepsUs) {
        if (candidate >= targetMajorStep) {
            majorStepUs = candidate;
            break;
        }
    }

    const minorStepUs = Math.max(1_000, Math.floor(majorStepUs / minorDivisions));
    const majorTicks: number[] = [];
    const minorTicks: number[] = [];

    const majorStart = Math.floor(startUs / majorStepUs) * majorStepUs;
    for (let timeUs = majorStart; timeUs <= endUs; timeUs += majorStepUs) {
        if (timeUs >= startUs) {
            majorTicks.push(timeUs);
        }
    }

    const minorStart = Math.floor(startUs / minorStepUs) * minorStepUs;
    for (let timeUs = minorStart; timeUs <= endUs; timeUs += minorStepUs) {
        if (timeUs >= startUs && timeUs % majorStepUs !== 0) {
            minorTicks.push(timeUs);
        }
    }

    return { majorTicks, minorTicks, majorStepUs, minorStepUs };
}
