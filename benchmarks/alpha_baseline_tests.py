"""Regression checks for paired report integrity (alpha plan section 6.2).

Run with Python's standard library; no renderer or archived build is required.
"""
import json
from pathlib import Path
import tempfile
import unittest

from compare_alpha_baseline import compare


class AlphaBaselineTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='lumen-alpha-baseline-')
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        for label, timings in (('before', (10, 20, 30)), ('after', (20, 10, 45))):
            self.write(f'{label}-manifest.json', {
                'revision': label + '-source', 'platform': 'test-platform',
                'machine': 'test-machine', 'processor': 'test-processor', 'backend': 'cpu',
                'executables': {'lumen-alpha-bench': {'path': label, 'sha256': '0' * 64}},
            })
            for group, timing in enumerate(timings, 1):
                self.write(f'{label}-layers-{group}.json', {
                    'benchmark': 'lumen-alpha-bench', 'revision': label + '-source',
                    'group': group, 'backend': 'cpu', 'scenario': 'layers',
                    'build_type': 'Release', 'warmup_frames': 30, 'measured_frames': 300,
                    'presentation': 'none', 'physical_pixels': [1920, 1080],
                    'frame_hash': label + '-pixels',
                    'alpha_mode': 'straight' if label == 'before' else 'premultiplied',
                    'phases': {'submit': {'p50_us': timing, 'p95_us': 2 * timing}},
                })

    def write(self, name, data):
        (self.directory / name).write_text(json.dumps(data), encoding='utf-8')

    def change(self, name, edit):
        data = json.loads((self.directory / name).read_text(encoding='utf-8'))
        edit(data)
        self.write(name, data)

    def test_complete_pairs_preserve_each_group_and_flag_regressions(self):
        result = compare(self.directory)
        self.assertEqual(len(result['runs']), 6)
        row = result['summary'][0]
        self.assertEqual(row['groups'], [1, 2, 3])
        self.assertEqual(row['delta_percent'], [100, -50, 50])
        self.assertEqual(row['median_paired_delta_percent'], 50)
        self.assertEqual(row['median_delta_percent'], 0)
        self.assertEqual(row['review_groups'], [1, 3])

    def test_mixed_source_revision_is_rejected(self):
        self.change('after-layers-3.json', lambda run: run.update(revision='other-source'))
        with self.assertRaisesRegex(ValueError, 'source manifest'):
            compare(self.directory)

    def test_canonical_embedded_commit_must_match_manifest(self):
        self.change('after-layers-3.json', lambda run: run.update(commit='other-source'))
        with self.assertRaisesRegex(ValueError, 'source manifest'):
            compare(self.directory)

    def test_group_identity_must_match_filename(self):
        self.change('after-layers-3.json', lambda run: run.update(group=2))
        with self.assertRaisesRegex(ValueError, 'manifest/group'):
            compare(self.directory)

    def test_different_measurement_hosts_are_rejected(self):
        self.change('after-manifest.json', lambda manifest: manifest.update(machine='other-host'))
        with self.assertRaisesRegex(ValueError, 'manifest machine'):
            compare(self.directory)

    def test_matching_peer_backend_drift_is_rejected(self):
        for label in ('before', 'after'):
            self.change(f'{label}-layers-2.json', lambda run: run.update(backend='skia'))
        with self.assertRaisesRegex(ValueError, 'source manifest'):
            compare(self.directory)

    def test_unrecorded_executable_is_rejected(self):
        self.change('after-layers-1.json', lambda run: run.update(benchmark='unrecorded-bench'))
        with self.assertRaisesRegex(ValueError, 'source manifest'):
            compare(self.directory)

    def test_matching_new_phase_in_later_group_is_not_silently_dropped(self):
        for label in ('before', 'after'):
            self.change(f'{label}-layers-2.json', lambda run: run['phases'].update(
                upload={'p50_us': 100, 'p95_us': 200}))
        with self.assertRaisesRegex(ValueError, 'phases'):
            compare(self.directory)

    def test_missing_pair_member_is_rejected(self):
        (self.directory / 'after-layers-3.json').unlink()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            compare(self.directory)

    def test_pending_pair_is_rejected_even_when_both_json_files_exist(self):
        (self.directory / 'layers-3.pending').write_text('pair in progress', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'unfinished'):
            compare(self.directory)


if __name__ == '__main__':
    unittest.main()
