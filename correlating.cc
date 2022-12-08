// My correlating predictor cpp file
#include "cpu/pred/correlating.hh"

#include "base/bitfield.hh"
#include "base/intmath.hh"
#include "debug/Corr.hh"

namespace gem5
{

namespace branch_prediction
{

CorrelatingBP::CorrelatingBP(const CorrelatingBPParams &params)
    : BPredUnit(params),
    corrCtrBits(params.localCtrBits),
    corrHistoryLength(params.localHistoryLength),
    corrHistoryTableSizeE(params.localHistoryTableSize),
    corrPredictorEntrySizeC(pow(2,corrHistoryLength)),
    corrPredictorEntrySizeB(corrPredictorEntrySizeC * corrCtrBits),
    corrPredictorSizeE(corrHistoryTableSizeE),
    corrPredictorSizeC(corrPredictorSizeE * corrPredictorEntrySizeC),
    corrPredictorSizeB(corrPredictorSizeE * corrPredictorEntrySizeB),
    corrPatternTable(corrPredictorSizeC, SatCounter8(corrCtrBits))
{
    if (!isPowerOf2(corrPredictorSizeC)) {
        fatal("Invalid predictor size!\n");
    }
    DPRINTF(Corr, "hello\n");
    corrPredictorMask = mask(corrHistoryLength);

    if (!isPowerOf2(corrHistoryTableSizeE)) {
        fatal("Invalid history table size!\n");
    }
    // now initialize the tables
    corrHistoryTable.resize(corrHistoryTableSizeE);
    for (int i = 0; i < corrHistoryTableSizeE; i++) {
        corrHistoryTable[i] = 0;
    }

    // set the threshold for a taken prediction
    corrThreshold = (1ULL << (corrCtrBits - 1)) - 1;

    // Debug extravaganza:
    DPRINTF(Corr, "# of counters: %u\n", corrPredictorSizeC); 
    DPRINTF(Corr, "# of entries (hist, pred): %u, %u\n",
        corrHistoryTableSizeE, corrPredictorSizeE);
    DPRINTF(Corr, "size of predictor table (bits): %u\n", corrPredictorSizeB);
    DPRINTF(Corr, "predictor counters per entry: %u\n", corrPredictorEntrySizeC);
    DPRINTF(Corr, "predictor mask: %u\n", corrPredictorMask);
}

inline
unsigned
CorrelatingBP::calcHistIdx(Addr &branch_addr)
{
    // we index the history table with the lower order bits
    // of the branch instruction address
    return (branch_addr >> instShiftAmt) & (corrHistoryTableSizeE - 1);
}

inline
void
CorrelatingBP::updateHist(unsigned idx, bool taken)
{
    corrHistoryTable[idx] = 
        (corrHistoryTable[idx] << 1) | taken;
}

inline
void 
CorrelatingBP::btbUpdate(ThreadID tid, Addr branch_addr, void * &bp_history)
{
    // find where in the table we should put the new entry
    unsigned new_idx = calcHistIdx(branch_addr);
    // now we set that register's most recent value to zero/not taken
    corrHistoryTable[new_idx] &= (corrPredictorMask & ~1ULL);

}

bool
CorrelatingBP::lookup(ThreadID tid, Addr branch_addr, void * &bp_history)
{
    DPRINTF(Corr, "branch addr: %" PRIu64" \n", branch_addr);
    // first get the index to look for the history in
    unsigned history_idx = calcHistIdx(branch_addr);
    DPRINTF(Corr, "history index: %u\n", history_idx);
    // now get the history for that index
    assert(history_idx < corrHistoryTableSizeE);
    unsigned history_value = corrHistoryTable[history_idx] & corrPredictorMask;
    DPRINTF(Corr, "history entry's value: %u\n", history_value);
    // now index the pattern history table, using the history index as the row
    // index and the history value as the column index
    unsigned predictor_idx = history_idx << corrHistoryLength |
                             history_value;
    DPRINTF(Corr, "predictor table index: %u\n", predictor_idx);
    assert(predictor_idx < corrPredictorSizeC);
    bool prediction = corrPatternTable[predictor_idx] > corrThreshold;
    DPRINTF(Corr, "prediction counter: %u\n", 
        static_cast<unsigned>(corrPatternTable[predictor_idx]));
    // Create BPHistory and send it to be recorded
    BPHistory *history = new BPHistory;
    history->corrHistoryIdx = history_idx;
    history->corrHistory = history_value;
    history->corrPredTaken = prediction;
    bp_history = (void *)history;
//    DPRINTF(Corr, "history creation complete\n");
    // Speculatively update the local history
    // if it's wrong, we'll have to squash it later
    updateHist(history_idx, prediction);
    DPRINTF(Corr, "lookup complete\n");
    return prediction;
    
}

void
CorrelatingBP::uncondBranch(ThreadID tid, Addr pc, void * &bp_history)
{
    // We don't need to actually make a prediction, just set a new BPHistory
    BPHistory * history = new BPHistory;
    history->corrHistory = invalidPredictorIdx;
    history->corrHistoryIdx = invalidPredictorIdx;
    history->corrPredTaken = true;
    bp_history = static_cast<void *>(history);
}

void
CorrelatingBP::update(ThreadID tid, Addr branch_addr, bool taken,
                      void *bp_history, bool squashed,
                      const StaticInstPtr & inst, Addr corrTarget)
{
    // here we update the predictor (pattern history) table
    // we also have to recover if our prediction was wrong
    assert(bp_history);

    BPHistory *history = static_cast<BPHistory *>(bp_history);
    // find the history table index for the last prediction
    unsigned history_idx = calcHistIdx(branch_addr);
    DPRINTF(Corr, "update() on history index %u\n", history_idx);
    DPRINTF(Corr, "index's corrHistoryIdx: %u\n", history->corrHistoryIdx);
    DPRINTF(Corr, "index's corrHistory value: %u\n", history->corrHistory); 
    // the index should not be out of the table's range
    assert(history_idx < corrHistoryTableSizeE);
    // if the branch was unconditional, then we won't update the table
    bool is_conditional = (history->corrHistory != invalidPredictorIdx);
    DPRINTF(Corr, "is_conditional: %d\n", is_conditional); 
    // we already speculative updated the history table
    // if we were wrong, we have to correct the table here
    if (squashed) {
        if (is_conditional) {
           corrHistoryTable[history_idx] = 
                   (history->corrHistory << 1) | taken;
        }
        DPRINTF(Corr, "Update complete: squashed\n");
        return;
    }
    // recall: history value is the index of the predictor table
    unsigned history_value = history->corrHistory & corrPredictorMask;
    // now we need to updating the relevant saturating counter
    // the counter table is 2D; the history value is the column,
    // and the branch address is the row
    unsigned predictor_idx = 
        (history_idx << corrHistoryLength) | history_value;
    assert(predictor_idx < corrPredictorSizeC);
    if (taken) {
        if (is_conditional) {
            corrPatternTable[predictor_idx]++;
        }
    } else {
        if (is_conditional) {
            corrPatternTable[predictor_idx]--;
        }
    }

    delete history;
    DPRINTF(Corr,"Update complete\n");
}

void
CorrelatingBP::squash(ThreadID tid, void *bp_history)
{
    BPHistory * history = static_cast<BPHistory *>(bp_history);
    DPRINTF(Corr, "squash() on history index %u\n", history->corrHistoryIdx);
    if (history->corrHistoryIdx != invalidPredictorIdx) {
        corrHistoryTable[history->corrHistoryIdx] = history->corrHistory;
    }
    delete history;
}

#ifdef DEBUG
int
CorrelatingBP::BPHistory::newCount = 0;
#endif


} // namespace branch_prediction

} // namespace gem5
