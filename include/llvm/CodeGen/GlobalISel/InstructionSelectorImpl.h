//===- llvm/CodeGen/GlobalISel/InstructionSelectorImpl.h --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API for the instruction selector.
/// This class is responsible for selecting machine instructions.
/// It's implemented by the target. It's used by the InstructionSelect pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTORIMPL_H
#define LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTORIMPL_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetOpcodes.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

/// GlobalISel PatFrag Predicates
enum {
  GIPFP_Invalid,
};

template <class TgtInstructionSelector, class PredicateBitset,
          class ComplexMatcherMemFn>
bool InstructionSelector::executeMatchTable(
    TgtInstructionSelector &ISel, NewMIVector &OutMIs, MatcherState &State,
    const MatcherInfoTy<PredicateBitset, ComplexMatcherMemFn> &MatcherInfo,
    const int64_t *MatchTable, const TargetInstrInfo &TII,
    MachineRegisterInfo &MRI, const TargetRegisterInfo &TRI,
    const RegisterBankInfo &RBI,
    const PredicateBitset &AvailableFeatures) const {
  uint64_t CurrentIdx = 0;
  SmallVector<uint64_t, 8> OnFailResumeAt;

  enum RejectAction { RejectAndGiveUp, RejectAndResume };
  auto handleReject = [&]() -> RejectAction {
    DEBUG(dbgs() << CurrentIdx << ": Rejected\n");
    if (OnFailResumeAt.empty())
      return RejectAndGiveUp;
    CurrentIdx = OnFailResumeAt.back();
    OnFailResumeAt.pop_back();
    DEBUG(dbgs() << CurrentIdx << ": Resume at " << CurrentIdx << " ("
                 << OnFailResumeAt.size() << " try-blocks remain)\n");
    return RejectAndResume;
  };

  while (true) {
    assert(CurrentIdx != ~0u && "Invalid MatchTable index");
    switch (MatchTable[CurrentIdx++]) {
    case GIM_Try: {
      DEBUG(dbgs() << CurrentIdx << ": Begin try-block\n");
      OnFailResumeAt.push_back(MatchTable[CurrentIdx++]);
      break;
    }

    case GIM_RecordInsn: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];

      // As an optimisation we require that MIs[0] is always the root. Refuse
      // any attempt to modify it.
      assert(NewInsnID != 0 && "Refusing to modify MIs[0]");

      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isReg()) {
        DEBUG(dbgs() << CurrentIdx << ": Not a register\n");
        if (handleReject() == RejectAndGiveUp)
          return false;
        break;
      }
      if (TRI.isPhysicalRegister(MO.getReg())) {
        DEBUG(dbgs() << CurrentIdx << ": Is a physical register\n");
        if (handleReject() == RejectAndGiveUp)
          return false;
        break;
      }

      MachineInstr *NewMI = MRI.getVRegDef(MO.getReg());
      if ((size_t)NewInsnID < State.MIs.size())
        State.MIs[NewInsnID] = NewMI;
      else {
        assert((size_t)NewInsnID == State.MIs.size() &&
               "Expected to store MIs in order");
        State.MIs.push_back(NewMI);
      }
      DEBUG(dbgs() << CurrentIdx << ": MIs[" << NewInsnID
                   << "] = GIM_RecordInsn(" << InsnID << ", " << OpIdx
                   << ")\n");
      break;
    }

    case GIM_CheckFeatures: {
      int64_t ExpectedBitsetID = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckFeatures(ExpectedBitsetID="
                   << ExpectedBitsetID << ")\n");
      if ((AvailableFeatures & MatcherInfo.FeatureBitsets[ExpectedBitsetID]) !=
          MatcherInfo.FeatureBitsets[ExpectedBitsetID]) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckOpcode: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Expected = MatchTable[CurrentIdx++];

      unsigned Opcode = State.MIs[InsnID]->getOpcode();
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckOpcode(MIs[" << InsnID
                   << "], ExpectedOpcode=" << Expected << ") // Got=" << Opcode
                   << "\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (Opcode != Expected) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckNumOperands: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Expected = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckNumOperands(MIs[" << InsnID
                   << "], Expected=" << Expected << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (State.MIs[InsnID]->getNumOperands() != Expected) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckImmPredicate: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Predicate = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckImmPredicate(MIs[" << InsnID
                   << "], Predicate=" << Predicate << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      assert(State.MIs[InsnID]->getOpcode() == TargetOpcode::G_CONSTANT &&
             "Expected G_CONSTANT");
      assert(Predicate > GIPFP_Invalid && "Expected a valid predicate");
      int64_t Value = 0;
      if (State.MIs[InsnID]->getOperand(1).isCImm())
        Value = State.MIs[InsnID]->getOperand(1).getCImm()->getSExtValue();
      else if (State.MIs[InsnID]->getOperand(1).isImm())
        Value = State.MIs[InsnID]->getOperand(1).getImm();
      else
        llvm_unreachable("Expected Imm or CImm operand");

      if (!MatcherInfo.ImmPredicateFns[Predicate](Value))
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }

    case GIM_CheckType: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t TypeID = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckType(MIs[" << InsnID
                   << "]->getOperand(" << OpIdx << "), TypeID=" << TypeID
                   << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (MRI.getType(State.MIs[InsnID]->getOperand(OpIdx).getReg()) !=
          MatcherInfo.TypeObjects[TypeID]) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckRegBankForClass: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t RCEnum = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckRegBankForClass(MIs[" << InsnID
                   << "]->getOperand(" << OpIdx << "), RCEnum=" << RCEnum
                   << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (&RBI.getRegBankFromRegClass(*TRI.getRegClass(RCEnum)) !=
          RBI.getRegBank(State.MIs[InsnID]->getOperand(OpIdx).getReg(), MRI,
                         TRI)) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckComplexPattern: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t RendererID = MatchTable[CurrentIdx++];
      int64_t ComplexPredicateID = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": State.Renderers[" << RendererID
                   << "] = GIM_CheckComplexPattern(MIs[" << InsnID
                   << "]->getOperand(" << OpIdx
                   << "), ComplexPredicateID=" << ComplexPredicateID << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      // FIXME: Use std::invoke() when it's available.
      if (!(State.Renderers[RendererID] =
                (ISel.*MatcherInfo.ComplexPredicates[ComplexPredicateID])(
                    State.MIs[InsnID]->getOperand(OpIdx)))) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckConstantInt: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t Value = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckConstantInt(MIs[" << InsnID
                   << "]->getOperand(" << OpIdx << "), Value=" << Value
                   << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!isOperandImmEqual(State.MIs[InsnID]->getOperand(OpIdx), Value,
                             MRI)) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckLiteralInt: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t Value = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckLiteralInt(MIs[" << InsnID
                   << "]->getOperand(" << OpIdx << "), Value=" << Value
                   << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isCImm() || !MO.getCImm()->equalsInt(Value)) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckIntrinsicID: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t Value = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckIntrinsicID(MIs[" << InsnID
                   << "]->getOperand(" << OpIdx << "), Value=" << Value
                   << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isIntrinsicID() || MO.getIntrinsicID() != Value)
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }

    case GIM_CheckIsMBB: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckIsMBB(MIs[" << InsnID
                   << "]->getOperand(" << OpIdx << "))\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!State.MIs[InsnID]->getOperand(OpIdx).isMBB()) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckIsSafeToFold: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      DEBUG(dbgs() << CurrentIdx << ": GIM_CheckIsSafeToFold(MIs[" << InsnID
                   << "])\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!isObviouslySafeToFold(*State.MIs[InsnID])) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_Reject:
      DEBUG(dbgs() << CurrentIdx << ": GIM_Reject");
      if (handleReject() == RejectAndGiveUp)
        return false;
      break;

    case GIR_MutateOpcode: {
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t NewOpcode = MatchTable[CurrentIdx++];
      assert((size_t)NewInsnID == OutMIs.size() &&
             "Expected to store MIs in order");
      OutMIs.push_back(
          MachineInstrBuilder(*State.MIs[OldInsnID]->getParent()->getParent(),
                              State.MIs[OldInsnID]));
      OutMIs[NewInsnID]->setDesc(TII.get(NewOpcode));
      DEBUG(dbgs() << CurrentIdx << ": GIR_MutateOpcode(OutMIs[" << NewInsnID
                   << "], MIs[" << OldInsnID << "], " << NewOpcode << ")\n");
      break;
    }

    case GIR_BuildMI: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Opcode = MatchTable[CurrentIdx++];
      assert((size_t)InsnID == OutMIs.size() &&
             "Expected to store MIs in order");
      (void)InsnID;
      OutMIs.push_back(BuildMI(*State.MIs[0]->getParent(), State.MIs[0],
                               State.MIs[0]->getDebugLoc(), TII.get(Opcode)));
      DEBUG(dbgs() << CurrentIdx << ": GIR_BuildMI(OutMIs[" << InsnID << "], "
                   << Opcode << ")\n");
      break;
    }

    case GIR_Copy: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      OutMIs[NewInsnID].add(State.MIs[OldInsnID]->getOperand(OpIdx));
      DEBUG(dbgs() << CurrentIdx << ": GIR_Copy(OutMIs[" << NewInsnID
                   << "], MIs[" << OldInsnID << "], " << OpIdx << ")\n");
      break;
    }

    case GIR_CopySubReg: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t SubRegIdx = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      OutMIs[NewInsnID].addReg(State.MIs[OldInsnID]->getOperand(OpIdx).getReg(),
                               0, SubRegIdx);
      DEBUG(dbgs() << CurrentIdx << ": GIR_CopySubReg(OutMIs[" << NewInsnID
                   << "], MIs[" << OldInsnID << "], " << OpIdx << ", "
                   << SubRegIdx << ")\n");
      break;
    }

    case GIR_AddImplicitDef: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RegNum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addDef(RegNum, RegState::Implicit);
      DEBUG(dbgs() << CurrentIdx << ": GIR_AddImplicitDef(OutMIs[" << InsnID
                   << "], " << RegNum << ")\n");
      break;
    }

    case GIR_AddImplicitUse: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RegNum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addUse(RegNum, RegState::Implicit);
      DEBUG(dbgs() << CurrentIdx << ": GIR_AddImplicitUse(OutMIs[" << InsnID
                   << "], " << RegNum << ")\n");
      break;
    }

    case GIR_AddRegister: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RegNum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addReg(RegNum);
      DEBUG(dbgs() << CurrentIdx << ": GIR_AddRegister(OutMIs[" << InsnID
                   << "], " << RegNum << ")\n");
      break;
    }

    case GIR_AddImm: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Imm = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addImm(Imm);
      DEBUG(dbgs() << CurrentIdx << ": GIR_AddImm(OutMIs[" << InsnID << "], "
                   << Imm << ")\n");
      break;
    }

    case GIR_ComplexRenderer: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RendererID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      State.Renderers[RendererID](OutMIs[InsnID]);
      DEBUG(dbgs() << CurrentIdx << ": GIR_ComplexRenderer(OutMIs[" << InsnID
                   << "], " << RendererID << ")\n");
      break;
    }

    case GIR_CopyConstantAsSImm: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      assert(State.MIs[OldInsnID]->getOpcode() == TargetOpcode::G_CONSTANT && "Expected G_CONSTANT");
      if (State.MIs[OldInsnID]->getOperand(1).isCImm()) {
        OutMIs[NewInsnID].addImm(
            State.MIs[OldInsnID]->getOperand(1).getCImm()->getSExtValue());
      } else if (State.MIs[OldInsnID]->getOperand(1).isImm())
        OutMIs[NewInsnID].add(State.MIs[OldInsnID]->getOperand(1));
      else
        llvm_unreachable("Expected Imm or CImm operand");
      DEBUG(dbgs() << CurrentIdx << ": GIR_CopyConstantAsSImm(OutMIs[" << NewInsnID
                   << "], MIs[" << OldInsnID << "])\n");
      break;
    }

    case GIR_ConstrainOperandRC: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t RCEnum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      constrainOperandRegToRegClass(*OutMIs[InsnID].getInstr(), OpIdx,
                                    *TRI.getRegClass(RCEnum), TII, TRI, RBI);
      DEBUG(dbgs() << CurrentIdx << ": GIR_ConstrainOperandRC(OutMIs[" << InsnID
                   << "], " << OpIdx << ", " << RCEnum << ")\n");
      break;
    }

    case GIR_ConstrainSelectedInstOperands: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      constrainSelectedInstRegOperands(*OutMIs[InsnID].getInstr(), TII, TRI,
                                       RBI);
      DEBUG(dbgs() << CurrentIdx
                   << ": GIR_ConstrainSelectedInstOperands(OutMIs[" << InsnID
                   << "])\n");
      break;
    }

    case GIR_MergeMemOperands: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");

      DEBUG(dbgs() << CurrentIdx << ": GIR_MergeMemOperands(OutMIs[" << InsnID
                   << "]");
      int64_t MergeInsnID = GIU_MergeMemOperands_EndOfList;
      while ((MergeInsnID = MatchTable[CurrentIdx++]) !=
             GIU_MergeMemOperands_EndOfList) {
        DEBUG(dbgs() << ", MIs[" << MergeInsnID << "]");
        for (const auto &MMO : State.MIs[MergeInsnID]->memoperands())
          OutMIs[InsnID].addMemOperand(MMO);
      }
      DEBUG(dbgs() << ")\n");
      break;
    }

    case GIR_EraseFromParent: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      assert(State.MIs[InsnID] &&
             "Attempted to erase an undefined instruction");
      State.MIs[InsnID]->eraseFromParent();
      DEBUG(dbgs() << CurrentIdx << ": GIR_EraseFromParent(MIs[" << InsnID
                   << "])\n");
      break;
    }

    case GIR_Done:
      DEBUG(dbgs() << CurrentIdx << ": GIR_Done");
      return true;

    default:
      llvm_unreachable("Unexpected command");
    }
  }
}

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTORIMPL_H
