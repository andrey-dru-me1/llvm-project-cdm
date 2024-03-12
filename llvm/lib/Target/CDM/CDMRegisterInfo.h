//
// Created by ilya on 21.11.23.
//

#ifndef LLVM_CDMREGISTERINFO_H
#define LLVM_CDMREGISTERINFO_H

#include "CDM.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "CDMGenRegisterInfo.inc"

namespace llvm {

class CDMRegisterInfo: public CDMGenRegisterInfo {
public:
  explicit CDMRegisterInfo();
  const MCPhysReg* getCalleeSavedRegs(const MachineFunction *MF) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned int FIOperandNum,
                           RegScavenger *RS) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;

private:
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID id) const override;
};

} // namespace llvm

#endif // LLVM_CDMREGISTERINFO_H
