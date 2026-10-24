"""Guard benchmark aggregation and comparison of every measured result."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('benchmark', ROOT / 'scripts/benchmark.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)

class BenchmarkTests(unittest.TestCase):
    def test_first_run_is_separate_from_repeat_median_and_memory_is_peak(self):
        rows = [dict(case='office', version='current', iteration=i, wall_ms=wall, cpu_ms=cpu,
                     peak_commit_bytes=commit, sampled_sum_rss_bytes=rss)
                for i, wall, cpu, commit, rss in [(0,900,300,50,80),(1,100,50,90,120),(2,200,100,70,100)]]
        summary = benchmark.summarize(rows)['office']['current']
        self.assertEqual(summary['first_wall_ms'], 900)
        self.assertEqual(summary['repeat_median_wall_ms'], 150)
        self.assertEqual(summary['median_wall_ms'], 200)
        self.assertEqual(summary['peak_commit_bytes'], 90)
        self.assertEqual(summary['sampled_sum_rss_bytes'], 120)
        self.assertAlmostEqual(summary['cpu_one_core_percent'], 37.5)

    def test_single_run_does_not_invent_a_repeat_measurement(self):
        row = dict(case='ocr',version='current',iteration=0,wall_ms=100,cpu_ms=40,
                   peak_commit_bytes=1,sampled_sum_rss_bytes=2)
        self.assertIsNone(benchmark.summarize([row])['ocr']['current']['repeat_median_wall_ms'])

    def test_later_result_mismatch_cannot_hide_behind_matching_first_result(self):
        with tempfile.TemporaryDirectory(prefix='benchmark-contract-',dir=ROOT/'build') as folder:
            root=Path(folder);before=root/'before.txt';same=root/'same.txt';changed=root/'changed.txt'
            before.write_bytes(b'correct');same.write_bytes(b'correct');changed.write_bytes(b'changed')
            with self.assertRaises(AssertionError):
                benchmark.compare_all_outputs({'baseline':[before],'current':[same,changed]},'txt',None,root)

if __name__ == '__main__': unittest.main()
