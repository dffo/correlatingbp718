# correlatingbp718
Correlating branch predictor implemented in gem5

# Details
All of these files should be placed in the directory `gem5/src/cpu/pred`.
The files `SConscript` and `BranchPredictor.py` replace the files already present in the default `gem5` repository.

The `correlating.hh` and `correlating.cc` are completely new, and contain the code defining the correlating predictor.
