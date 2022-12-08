// header file for correlative predictor

#ifndef __CPU_PRED_CORRELATING_PRED_HH__
#define __CPU_PRED_CORRELATING_PRED_HH__

#include <vector>

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "cpu/pred/bpred_unit.hh"
#include "params/CorrelatingBP.hh"

namespace gem5
{

namespace branch_prediction
{

class CorrelatingBP : public BPredUnit
{
  public:
    /**
     * Default branch predictor constructor
     */ 
    CorrelatingBP(const CorrelatingBPParams &params);

    bool lookup(ThreadID tid, Addr branch_addr, void * &bp_history);
    void uncondBranch(ThreadID tid, Addr pc, void * &bp_history);
    /** If a BTB entry is invalid/not found, make place in the
      * branch predictor tables and set its prediction to "not taken" */
    void btbUpdate(ThreadID tid, Addr branch_addr, void * &bp_history);
    void update(ThreadID tid, Addr branch_addr, bool taken, void *bp_history,
                bool squashed, const StaticInstPtr & inst, Addr corrTarget);
    void squash(ThreadID tid, void *bp_history);

  private:
    // takes a saturating counter value, returns true if above threshold
    // inline bool getPrediction(uint8_t &count);
    inline unsigned calcHistIdx(Addr &branch_addr);
    inline void updateHist(unsigned idx, bool taken);

    struct BPHistory
    {
#ifdef DEBUG
      BPHistory()
      { newCount++; }
      ~BPHistory()
      { newCount--; }
      static int newCount;
#endif
      unsigned corrHistoryIdx;
      unsigned corrHistory;
      bool corrPredTaken;
    };

    /** Invalid predictor index flag */
    static const int invalidPredictorIdx = -1;

    unsigned corrCtrBits; // bits per ctr
    /** Number of bits per history table entry */
    unsigned corrHistoryLength;
    /** Number of history table entries */
    unsigned corrHistoryTableSizeE;
    unsigned corrPredictorEntrySizeC; // ctrs per entry
    unsigned corrPredictorEntrySizeB; // bits per entry
    unsigned corrPredictorSizeE; // entries per predictor
    unsigned corrPredictorSizeC; // ctrs per predictor
    unsigned corrPredictorSizeB; // bits per predictor
    unsigned corrPredictorMask;
    /** The vector containing the 2D saturating counter table */
    std::vector<SatCounter8> corrPatternTable;
    /** The history register table  */
    std::vector<unsigned> corrHistoryTable;
    /** Threshold for branch taken */
    unsigned corrThreshold;
};

} // namespace branch_prediction
} // namespace gem5

#endif //__CPU_PRED_CORRELATING_PRED_HH__
