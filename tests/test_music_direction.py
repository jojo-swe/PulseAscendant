"""Synthetic tests for musical direction: strong attacks, chapter bounds and impossible edits."""
import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from direct_music import direct

class DirectionTests(unittest.TestCase):
    def test_attacks_selected_with_safe_spacing(self):
        data=np.zeros((801,3),dtype=np.uint8)
        for t in (9.25,19.75,29.15):
            data[round(t*20),2]=255
        starts,report=direct(data,np.zeros((801,24),dtype=np.uint8),[0,10,20,30],40)
        self.assertEqual(starts,[0,9.25,19.75,29.15])
        self.assertTrue(all(5<=b-a<=18 for a,b in zip(starts,starts[1:]+[40])))
        self.assertTrue(all(c['attack']==1 for c in report[1:]))
    def test_silence_remains_finite_and_uses_editorial_targets(self):
        starts,_=direct(np.zeros((601,3),dtype=np.uint8),np.zeros((601,24),dtype=np.uint8),[0,10,20],30)
        self.assertEqual(starts,[0,10,20])
    def test_impossible_edit_rejected(self):
        with self.assertRaises(ValueError):
            direct(np.zeros((201,3),dtype=np.uint8),np.zeros((201,24),dtype=np.uint8),[0,2,4],10)

if __name__=='__main__':
    unittest.main()
