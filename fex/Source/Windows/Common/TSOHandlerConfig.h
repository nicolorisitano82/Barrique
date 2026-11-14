// SPDX-License-Identifier: MIT
#pragma once

#include <FEXCore/Core/Context.h>
#include <FEXCore/Config/Config.h>
#include <FEXCore/Utils/ArchHelpers/Arm64.h>

#include "Windows/Common/FEXUnixLib.h"

namespace FEX::Windows {
class TSOHandlerConfig final {
public:
  TSOHandlerConfig(FEXCore::Context::Context& CTX) {
    if (HalfBarrierTSOEnabled()) {
      UnalignedHandlerType = FEXCore::ArchHelpers::Arm64::UnalignedHandlerType::HalfBarrier;
    } else {
      UnalignedHandlerType = FEXCore::ArchHelpers::Arm64::UnalignedHandlerType::NonAtomic;
    }

    if (TSOEnabled() && (FEX::Windows::UnixLib::TryEnableHardwareTSO() || TryEnableHostTSO())) {
      CTX.SetHardwareTSOSupport(true);
    }

    uint64_t Flags = (StrictInProcessSplitLocks() ? FEX_UNALIGN_ATOMIC_STRICT_SPLIT_LOCKS : 0) |
                     (KernelUnalignedAtomicBackpatching() ? FEX_UNALIGN_ATOMIC_BACKPATCH : 0) | FEX_UNALIGN_ATOMIC_EMULATE;

    if (UnixLib::SetKernelUnalignedAtomicControl(Flags)) {
      LogMan::Msg::IFmt("FEX: Kernel unaligned atomics enabled!");
    }
  }

  FEXCore::ArchHelpers::Arm64::UnalignedHandlerType GetUnalignedHandlerType() const {
    return UnalignedHandlerType;
  }

private:
  // Barrique: without the Linux unix library, ask Wine for it. Under Wine's
  // Hypervisor.framework backend on macOS this sets Apple's TSO bit on the vCPUs.
  static bool TryEnableHostTSO() {
    ULONG Enable = 1;
    return NtSetInformationProcess(NtCurrentProcess(), ProcessFexHardwareTso, &Enable, sizeof(Enable)) == STATUS_SUCCESS;
  }

  FEX_CONFIG_OPT(TSOEnabled, TSOENABLED);
  FEX_CONFIG_OPT(HalfBarrierTSOEnabled, HALFBARRIERTSOENABLED);
  FEX_CONFIG_OPT(StrictInProcessSplitLocks, STRICTINPROCESSSPLITLOCKS);
  FEX_CONFIG_OPT(KernelUnalignedAtomicBackpatching, KERNELUNALIGNEDATOMICBACKPATCHING);

  FEXCore::ArchHelpers::Arm64::UnalignedHandlerType UnalignedHandlerType {FEXCore::ArchHelpers::Arm64::UnalignedHandlerType::HalfBarrier};
};
} // namespace FEX::Windows
