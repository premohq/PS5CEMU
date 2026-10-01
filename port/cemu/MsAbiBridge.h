// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: the bridge between Cemu's x64 recompiler and the host on the PS5.
//
// Cemu's x64 backend emits code that calls the host, and is entered from the host, with the
// Microsoft x64 calling convention; on Linux and macOS the host functions involved are marked
// __attribute__((ms_abi)) (ATTR_MS_ABI). The x86_64-sie-ps5 target rejects ms_abi, so on the PS5
// those functions are ordinary SysV functions and the generated code reaches them through these
// thunks instead (MsAbiBridge.cpp). The backend itself is unchanged.

#pragma once

#include <cstdint>

extern "C"
{
	// Microsoft-ABI entry points that forward to the SysV host functions of the same name.
	void PS5_MsAbi_fres_espresso();
	void PS5_MsAbi_frsqrte_espresso();
	void PS5_MsAbi_PPCRecompiler_GetTBL();
	void PS5_MsAbi_PPCRecompiler_GetTBU();
	void PS5_MsAbi_PPCRecompiler_virtualHLE();

	// The generated enter function (Microsoft ABI), and a SysV function that calls it.
	extern void* PS5_MsAbi_enterRecompilerCodeTarget;
	void PS5_SysV_enterRecompilerCode(uint64_t codeMem, uint64_t ppcInterpreterInstance);
}

// The address generated code calls for host function F.
#define PPCREC_HOST_CALL(F) ((uintptr_t)&PS5_MsAbi_##F)
