"""Summarize the P0/P4 paired reports without changing tolerances or baselines.

The output retains every group's timings and flags >10% regressions for review.
A flag is not automatically excused as noise. CPU and Skia must run separately.
"""
import argparse
import json
from pathlib import Path
import re
from statistics import median
from run_alpha_baseline import valid_drawable


def compare(directory):
    if any(directory.glob('*.pending')):
        raise ValueError('unfinished benchmark pair; complete it with --resume before comparing')
    manifests = {label: json.loads((directory/f'{label}-manifest.json').read_text(encoding='utf-8'))
                 for label in ('before', 'after')}
    for field in ('platform', 'machine', 'processor', 'backend'):
        if manifests['before'][field] != manifests['after'][field]:
            raise ValueError(f'mismatched manifest {field}')
    runs = {}
    for path in directory.glob('*.json'):
        match = re.fullmatch(r'(before|after)-(.+)-(\d+)', path.stem)
        if match:
            label, case, group = match.groups()
            run = json.loads(path.read_text(encoding='utf-8'))
            manifest = manifests[label]
            if (run['revision'] != manifest['revision'] or
                    run.get('commit', run['revision']) != manifest['revision'] or
                    run['backend'] != manifest['backend'] or run['group'] != int(group) or
                    run['benchmark'] not in manifest['executables']):
                raise ValueError(f'{path.name}: report does not match its source manifest/group')
            runs[label, case, int(group)] = run
    cases = sorted({case for _, case, _ in runs})
    groups = sorted({group for _, _, group in runs})
    if len(groups) < 3 or not cases:
        raise ValueError('at least three complete groups are required')
    summary = []
    for case in cases:
        if any((label, case, group) not in runs
               for label in ('before', 'after') for group in groups):
            raise ValueError(f'{case}: incomplete before/after groups')
        pairs = [(runs['before', case, group], runs['after', case, group]) for group in groups]
        reference = pairs[0][0]
        for before, after in pairs:
            if not valid_drawable(before) or not valid_drawable(after):
                raise ValueError(f'{case}: resized/minimized drawable is not a valid sample')
            for field in ('benchmark', 'backend', 'scenario', 'viewport', 'physical_pixels',
                          'clear_alpha', 'device_scale', 'build_type', 'toolchain',
                          'warmup_frames', 'measured_frames', 'presentation',
                          'window_drawable_pixels', 'vsync_requested', 'scope', 'measurement_scope'):
                if before.get(field) != after.get(field) or before.get(field) != reference.get(field):
                    raise ValueError(f'{case}: mismatched {field}')
            if (before['build_type'] != 'Release' or
                    set(before['phases']) != set(after['phases']) or
                    set(before['phases']) != set(reference['phases'])):
                raise ValueError(f'{case}: mismatched build/phases')
            if before['warmup_frames'] != 30 or before['measured_frames'] != 300:
                raise ValueError(f'{case}: expected warmup=30 and frames=300')
        for label in ('before', 'after'):
            if len({runs[label, case, group]['frame_hash'] for group in groups}) != 1:
                raise ValueError(f'{case}: unstable {label} frame hash')
            if len({runs[label, case, group]['alpha_mode'] for group in groups}) != 1:
                raise ValueError(f'{case}: unstable {label} alpha mode')
        for phase in pairs[0][0]['phases']:
            for quantile in ('p50_us', 'p95_us'):
                before = [pair[0]['phases'][phase][quantile] for pair in pairs]
                after = [pair[1]['phases'][phase][quantile] for pair in pairs]
                deltas = [a-b for b,a in zip(before,after)]
                ratios = [(a/b-1)*100 if b else (0 if not a else None) for b,a in zip(before,after)]
                b, a = median(before), median(after)
                row = dict(case=case, phase=phase, statistic=quantile, groups=groups,
                           before=before, after=after, delta_us=deltas, delta_percent=ratios,
                           median_before_us=b, median_after_us=a,
                           median_delta_percent=(a/b-1)*100 if b else (0 if not a else None),
                           median_paired_delta_percent=median(ratios) if all(r is not None for r in ratios) else None,
                           review_groups=[g for g,r in zip(groups,ratios) if r is None or r > 10])
                summary.append(row)
    compact = []
    for (label, case, group), run in sorted(runs.items()):
        compact.append(dict(label=label, case=case, **{k:v for k,v in run.items() if k!='command'}))
    return dict(environment=manifests, summary=summary, runs=compact)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    options = parser.parse_args()
    report = compare(options.directory)
    options.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    for row in report['summary']:
        if row['review_groups']:
            print(json.dumps(row))
