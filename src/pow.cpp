// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chrono>
#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include "consensus/consensus.h"

unsigned int GetEpochSeconds() {
    time_t seconds;
    seconds = time(NULL);
    return (unsigned int) seconds;
}

bool IsKeccakTime() {
    unsigned int currtime = GetEpochSeconds();
    return currtime >= KECCAK_TIME;
}

uint256 GetPowLimit(const Consensus::Params& params) {
    return IsKeccakTime() ? params.nKeccakPowLimit : params.powLimit;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    // creativecoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = params.DifficultyAdjustmentInterval()-1;
    if ((pindexLast->nHeight+1) != params.DifficultyAdjustmentInterval())
        blockstogoback = params.DifficultyAdjustmentInterval();

    // Go back by what we want to be 14 days worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < blockstogoback; i++)
        pindexFirst = pindexFirst->pprev;

    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);

}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    int height = pindexLast->nHeight;

    // Retarget
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    if (height >= params.nDigiShieldHeight) {
        const int64_t retargetTimespan = params.nPowTargetTimespan;
        const int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
        int64_t nModulatedTimespan = nActualTimespan;
        int64_t nMaxTimespan;
        int64_t nMinTimespan;

        nModulatedTimespan = retargetTimespan + (nModulatedTimespan - retargetTimespan) / 8;

        nMinTimespan = retargetTimespan - (retargetTimespan / 4);
        nMaxTimespan = retargetTimespan + (retargetTimespan / 2);

        if (nModulatedTimespan < nMaxTimespan)
            nModulatedTimespan = nMinTimespan;
        else if (nModulatedTimespan > nMaxTimespan)
            nModulatedTimespan = nMaxTimespan;

        bnOld = bnNew;
        bnNew *= nModulatedTimespan;
        bnNew /= retargetTimespan;

    } else {
        // Limit adjustment step
        int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
        if (nActualTimespan < params.nPowTargetTimespan/4)
            nActualTimespan = params.nPowTargetTimespan/4;
        if (nActualTimespan > params.nPowTargetTimespan*4)
            nActualTimespan = params.nPowTargetTimespan*4;


        // creativecoin: intermediate uint256 can overflow by 1 bit
        bool fShift = bnNew.bits() > 235;
        if (fShift)
            bnNew >>= 1;

        bnNew *= nActualTimespan;
        bnNew /= params.nPowTargetTimespan;
        if (fShift)
            bnNew <<= 1;
    }

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();

}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, uint32_t nTime, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    uint256 powLimit = params.powLimit;

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(powLimit)) {
        return error("fNegative: %d, bnTarget == 0: %d, fOverflow: %d, bnTarget > UintToArith256(powLimit): %d", fNegative, bnTarget == 0, fOverflow, bnTarget > UintToArith256(powLimit));
    }

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return error("UintToArith256(hash) > bnTarget: %d", UintToArith256(hash) > bnTarget);

    return true;
}
