/*******************************************************************************
GPU OPTIMIZED MONTE CARLO (GOMC) 2.31
Copyright (C) 2018  GOMC Group
A copy of the GNU General Public License can be found in the COPYRIGHT.txt
along with this program, also can be found at <http://www.gnu.org/licenses/>.
********************************************************************************/
#ifndef IREGROWTH_H
#define IREGROWTH_H


#include "MoveBase.h"
#include "TrialMol.h"

//#define DEBUG_MOVES

class Regrowth : public MoveBase
{
public:

  Regrowth(System &sys, StaticVals const& statV) :
    ffRef(statV.forcefield), molLookRef(sys.molLookupRef),
    MoveBase(sys, statV) {}

  virtual uint Prep(const double subDraw, const double movPerc);
  virtual uint Transform();
  virtual void CalcEn();
  virtual void Accept(const uint earlyReject, const uint step);
private:
  uint GetBoxAndMol(const double subDraw, const double movPerc);
  MolPick molPick;
  uint sourceBox, destBox;
  uint pStart, pLen;
  uint molIndex, kindIndex;

  double W_recip;
  double correct_old, correct_new;
  cbmc::TrialMol oldMol, newMol;
  Intermolecular recipDiff;
  MoleculeLookup & molLookRef;
  Forcefield const& ffRef;
};

inline uint Regrowth::GetBoxAndMol
(const double subDraw, const double movPerc)
{

#if ENSEMBLE == GCMC
  sourceBox = mv::BOX0;
  uint state = prng.PickMol(molIndex, kindIndex, sourceBox, subDraw, movPerc);
#else
  uint state = prng.PickMolAndBox(molIndex, kindIndex, sourceBox, subDraw,
                                  movPerc);
#endif

  //molecule will be removed and insert in same box
  destBox = sourceBox;

  if (state != mv::fail_state::NO_MOL_OF_KIND_IN_BOX) {
    pStart = pLen = 0;
    molRef.GetRangeStartLength(pStart, pLen, molIndex);
  }
  return state;
}

inline uint Regrowth::Prep(const double subDraw, const double movPerc)
{
  uint state = GetBoxAndMol(subDraw, movPerc);
  newMol = cbmc::TrialMol(molRef.kinds[kindIndex], boxDimRef, destBox);
  oldMol = cbmc::TrialMol(molRef.kinds[kindIndex], boxDimRef, sourceBox);
  oldMol.SetCoords(coordCurrRef, pStart);
  return state;
}


inline uint Regrowth::Transform()
{
  cellList.RemoveMol(molIndex, sourceBox, coordCurrRef);
  molRef.kinds[kindIndex].Regrowth(oldMol, newMol, molIndex);
  return mv::fail_state::NO_FAIL;
}

inline void Regrowth::CalcEn()
{
  // since number of molecules would not change in the box,
  W_recip = 1.0;
  correct_old = 0.0;
  correct_new = 0.0;

  if (newMol.GetWeight() != 0.0) {
    correct_new = calcEwald->SwapCorrection(newMol);
    correct_old = calcEwald->SwapCorrection(oldMol);
    recipDiff.energy = calcEwald->MolReciprocal(newMol.GetCoords(), molIndex,
                       sourceBox);
    //self energy is same
    W_recip = exp(-1.0 * ffRef.beta * (recipDiff.energy + correct_new -
                                       correct_old));
  }
}


inline void Regrowth::Accept(const uint rejectState, const uint step)
{
  bool result;
  //If we didn't skip the move calculation
  if(rejectState == mv::fail_state::NO_FAIL) {
    double Wo = oldMol.GetWeight();
    double Wn = newMol.GetWeight();
    double Wrat = Wn / Wo * W_recip;

    //safety to make sure move will be rejected in overlap case
    if((newMol.GetEnergy().real < 1.0e15) &&
        (oldMol.GetEnergy().real < 1.0e15)) {
      result = prng() < Wrat;
    } else
      result = false;


    if(result) {
      //Add rest of energy.
      sysPotRef.boxEnergy[sourceBox] -= oldMol.GetEnergy();
      sysPotRef.boxEnergy[destBox] += newMol.GetEnergy();
      //Add Reciprocal energy difference
      sysPotRef.boxEnergy[destBox].recip += recipDiff.energy;
      //Add correction energy
      sysPotRef.boxEnergy[sourceBox].correction -= correct_old;
      sysPotRef.boxEnergy[destBox].correction += correct_new;

      //Set coordinates, new COM; shift index to new box's list
      newMol.GetCoords().CopyRange(coordCurrRef, 0, pStart, pLen);
      comCurrRef.SetNew(molIndex, destBox);
      cellList.AddMol(molIndex, destBox, coordCurrRef);


      //Zero out box energies to prevent small number
      //errors in double.
      if (molLookRef.NumInBox(sourceBox) == 1) {
        sysPotRef.boxEnergy[sourceBox].inter = 0;
        sysPotRef.boxVirial[sourceBox].inter = 0;
        sysPotRef.boxEnergy[sourceBox].real = 0;
        sysPotRef.boxVirial[sourceBox].real = 0;
      }

      calcEwald->UpdateRecip(sourceBox);
      //Retotal
      sysPotRef.Total();
    } else {
      cellList.AddMol(molIndex, sourceBox, coordCurrRef);

      //when weight is 0, MolDestSwap() will not be executed, thus cos/sin
      //molRef will not be changed. Also since no memcpy, doing restore
      //results in memory overwrite
      if (newMol.GetWeight() != 0.0)
        calcEwald->RestoreMol(molIndex);
    }
  } else //else we didn't even try because we knew it would fail
    result = false;

  subPick = mv::GetMoveSubIndex(mv::REGROWTH, sourceBox);
  moveSetRef.Update(result, subPick, step);
}

#endif
