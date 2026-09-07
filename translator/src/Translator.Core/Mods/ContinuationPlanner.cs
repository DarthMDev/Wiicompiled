using System.Buffers.Binary;
using System.Text.Json;
using Translator.Core.Disassembly;
using Translator.Core.Parsing.Kamek;
using Translator.Core.Mods.Mkwii;

namespace Translator.Core.Mods;

public sealed record ContinuationEntry(
    uint Address,
    uint ContainingFunctionStart,
    uint ContainingFunctionEnd,
    string SectionName,
    uint SourceCommandAddress,
    KamekCommandId SourceCommandId,
    string Reason);

public sealed class ContinuationPlan
{
    public required IReadOnlyList<ContinuationEntry> Entries { get; init; }
}

public static class ContinuationPlanner
{
    private const uint BctrInstruction = 0x4E800420u;
    private const uint BlrInstruction = 0x4E800020u;
    private const int MaxTailJumpConstantLookbackBytes = 64;

    public static ContinuationPlan Build(KamekChunk chunk, BaseManifest baseManifest, uint moduleGuestBase)
    {
        var functionIndex = new BaseFunctionIndex(baseManifest.Functions);
        var entries = new Dictionary<uint, ContinuationEntry>();

        foreach (var command in chunk.Commands)
        {
            if (!IsBranchLikeTarget(command.Id) || command.Arguments.Count == 0)
            {
                continue;
            }

            var target = KamekAddress.Resolve(command.Arguments[0], moduleGuestBase);
            var section = FindSection(baseManifest, target);
            if (section is null || !section.Executable)
            {
                continue;
            }

            var function = functionIndex.FindContaining(target);
            if (function is null || function.Start == target)
            {
                continue;
            }

            entries.TryAdd(target, new ContinuationEntry(
                target,
                function.Start,
                function.End,
                section.Name,
                command.AddressIsRelative ? checked(moduleGuestBase + command.Address) : command.Address,
                command.Id,
                "branch-like Code.pul target lands inside a base function"));
        }

        return new ContinuationPlan
        {
            Entries = entries.Values.OrderBy(e => e.Address).ToList()
        };
    }

    public static ContinuationPlan AddModuleTailJumpContinuations(
        ContinuationPlan plan,
        BaseManifest baseManifest,
        uint moduleGuestBase,
        byte[] relocatedModuleImage)
    {
        if (relocatedModuleImage.Length < 4)
        {
            return plan;
        }

        var functionIndex = new BaseFunctionIndex(baseManifest.Functions);
        var entries = plan.Entries.ToDictionary(e => e.Address);

        foreach (var tailJump in DiscoverModuleTailJumps(moduleGuestBase, relocatedModuleImage))
        {
            var section = FindSection(baseManifest, tailJump.TargetAddress);
            if (section is null || !section.Executable)
            {
                continue;
            }

            var function = functionIndex.FindContaining(tailJump.TargetAddress);
            if (function is null || function.Start == tailJump.TargetAddress)
            {
                continue;
            }

            entries.TryAdd(tailJump.TargetAddress, new ContinuationEntry(
                tailJump.TargetAddress,
                function.Start,
                function.End,
                section.Name,
                tailJump.SourceAddress,
                KamekCommandId.Branch,
                "Kamek module tail jump lands inside a base function"));
        }

        return new ContinuationPlan
        {
            Entries = entries.Values.OrderBy(e => e.Address).ToList()
        };
    }

    public static ContinuationPlan AddRetroWfcExecutableHookContinuations(
        ContinuationPlan plan,
        BaseManifest baseManifest,
        IEnumerable<RetroWfcExecutableHookPlan> hooks)
    {
        var functionIndex = new BaseFunctionIndex(baseManifest.Functions);
        var entries = plan.Entries.ToDictionary(e => e.Address);

        foreach (var hook in hooks)
        {
            var target = hook.ContinuationAddress;
            var section = FindSection(baseManifest, target);
            if (section is null || !section.Executable)
            {
                continue;
            }

            var function = functionIndex.FindContaining(target);
            if (function is null || function.Start == target)
            {
                continue;
            }

            var action = hook.TargetActionId ?? string.Join(",", hook.SemanticActionIds);
            entries.TryAdd(target, new ContinuationEntry(
                target,
                function.Start,
                function.End,
                section.Name,
                hook.Address,
                KamekCommandId.Branch,
                $"Retro WFC executable hook continuation {action}"));
        }

        return new ContinuationPlan
        {
            Entries = entries.Values.OrderBy(e => e.Address).ToList()
        };
    }


    private static bool IsBranchLikeTarget(KamekCommandId id) =>
        id is KamekCommandId.Rel24 or KamekCommandId.Branch or KamekCommandId.BranchLink;

    private static BaseSectionMetadata? FindSection(BaseManifest manifest, uint address) =>
        manifest.Sections.FirstOrDefault(section => address >= section.GuestStart && address < section.GuestEnd);

    private static IEnumerable<ModuleTailJump> DiscoverModuleTailJumps(
        uint moduleGuestBase,
        byte[] relocatedModuleImage)
    {
        for (var offset = 0; offset + 4 <= relocatedModuleImage.Length; offset += 4)
        {
            var word = PpcWordFields.ReadBigEndianWord(relocatedModuleImage, offset);
            if (word != BctrInstruction && word != BlrInstruction)
            {
                continue;
            }

            var sprWriteOffset = offset - 4;
            if (sprWriteOffset < 0)
            {
                continue;
            }

            var sprWrite = PpcWordFields.ReadBigEndianWord(relocatedModuleImage, sprWriteOffset);
            int sourceRegister;
            var hasRegisterSource = word == BctrInstruction
                ? PpcInstructionPatterns.TryGetMtspr(sprWrite, 9, out sourceRegister)
                : PpcInstructionPatterns.TryGetMtspr(sprWrite, 8, out sourceRegister);
            if (!hasRegisterSource)
            {
                continue;
            }

            if (!TryResolveConstantRegisterValue(
                    relocatedModuleImage,
                    sprWriteOffset,
                    sourceRegister,
                    out var targetAddress))
            {
                continue;
            }

            yield return new ModuleTailJump(
                checked(moduleGuestBase + (uint)offset),
                targetAddress);
        }
    }

    private static bool TryResolveConstantRegisterValue(
        byte[] relocatedModuleImage,
        int beforeOffset,
        int register,
        out uint value)
    {
        var lowOperation = LowImmediateOperation.None;
        var lowImmediate = 0u;
        var scanStart = Math.Max(0, beforeOffset - MaxTailJumpConstantLookbackBytes);

        for (var offset = beforeOffset - 4; offset >= scanStart; offset -= 4)
        {
            var word = PpcWordFields.ReadBigEndianWord(relocatedModuleImage, offset);

            if (PpcInstructionPatterns.TryGetOri(word, out var oriSource, out var oriDestination, out var oriImmediate) &&
                oriDestination == register)
            {
                if (oriSource != register || lowOperation != LowImmediateOperation.None)
                {
                    break;
                }

                lowOperation = LowImmediateOperation.Or;
                lowImmediate = oriImmediate;
                continue;
            }

            if (PpcInstructionPatterns.TryGetAddi(word, out var addiDestination, out var addiSource, out var addiImmediate) &&
                addiDestination == register)
            {
                if (addiSource != register || lowOperation != LowImmediateOperation.None)
                {
                    break;
                }

                lowOperation = LowImmediateOperation.AddSigned;
                lowImmediate = unchecked((uint)addiImmediate);
                continue;
            }

            if (PpcInstructionPatterns.TryGetLis(word, out var lisDestination, out var highImmediate) &&
                lisDestination == register)
            {
                var baseValue = highImmediate << 16;
                value = lowOperation switch
                {
                    LowImmediateOperation.None => baseValue,
                    LowImmediateOperation.Or => baseValue | lowImmediate,
                    LowImmediateOperation.AddSigned => unchecked(baseValue + (uint)(short)lowImmediate),
                    _ => baseValue
                };
                return true;
            }

            if (PpcRegisterEffects.MayWriteGpr(word, register))
            {
                break;
            }
        }

        value = 0;
        return false;
    }

    private sealed record ModuleTailJump(uint SourceAddress, uint TargetAddress);

    private enum LowImmediateOperation
    {
        None,
        Or,
        AddSigned
    }

    public static IEnumerable<int> DiscoverLrRelativeIndirectJumpOffsets(IReadOnlyList<PpcInstruction> instructions)
    {
        var lrOffsets = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        int? ctrOffset = null;
        int? lrReturnOffset = null;

        foreach (var instruction in instructions)
        {
            var mnemonic = instruction.Mnemonic.ToLowerInvariant();
            if (mnemonic == "mflr" && TryGetInstructionReg(instruction, 0, out var lrDest))
            {
                lrOffsets[lrDest] = 0;
                continue;
            }

            if ((mnemonic == "mr" || mnemonic == "or") &&
                TryGetInstructionReg(instruction, 0, out var moveDest) &&
                TryGetInstructionReg(instruction, 1, out var moveSource) &&
                (mnemonic == "mr" ||
                 (instruction.Operands.Count >= 3 &&
                  instruction.Operands[2] is PpcRegisterOperand moveSource2 &&
                  string.Equals(NormalizeInstructionReg(moveSource2.Name), moveSource, StringComparison.OrdinalIgnoreCase))))
            {
                if (lrOffsets.TryGetValue(moveSource, out var sourceOffset))
                {
                    lrOffsets[moveDest] = sourceOffset;
                }
                else
                {
                    lrOffsets.Remove(moveDest);
                }
                continue;
            }

            if (mnemonic == "addi" &&
                TryGetInstructionReg(instruction, 0, out var addDest) &&
                TryGetInstructionReg(instruction, 1, out var addBase) &&
                TryGetInstructionImm(instruction, 2, out var imm))
            {
                if (lrOffsets.TryGetValue(addBase, out var baseOffset))
                {
                    lrOffsets[addDest] = checked(baseOffset + imm);
                }
                else
                {
                    lrOffsets.Remove(addDest);
                }
                continue;
            }

            if (mnemonic == "mtctr" && TryGetInstructionReg(instruction, 0, out var ctrSource))
            {
                ctrOffset = lrOffsets.TryGetValue(ctrSource, out var sourceOffset) ? sourceOffset : null;
                continue;
            }

            if (mnemonic == "bctr")
            {
                if (ctrOffset.HasValue)
                {
                    yield return ctrOffset.Value;
                }
                ctrOffset = null;
                continue;
            }

            if (mnemonic == "mtlr" && TryGetInstructionReg(instruction, 0, out var lrSource))
            {
                lrReturnOffset = lrOffsets.TryGetValue(lrSource, out var sourceOffset) ? sourceOffset : null;
                continue;
            }

            if (instruction.IsReturn || mnemonic == "blr" || mnemonic == "bclr" || (mnemonic.StartsWith("b", StringComparison.Ordinal) && mnemonic.EndsWith("lr", StringComparison.Ordinal)))
            {
                if (lrReturnOffset.HasValue && lrReturnOffset.Value != 0)
                {
                    yield return lrReturnOffset.Value;
                }
                lrReturnOffset = null;
                continue;
            }

            if (instruction.IsCall || mnemonic == "bl" || mnemonic == "blrl")
            {
                lrReturnOffset = null;
            }

            if (TryInstructionWritesDest(instruction, out var dest))
            {
                lrOffsets.Remove(dest);
            }
        }

        static bool TryGetInstructionReg(PpcInstruction instruction, int index, out string register)
        {
            if (instruction.Operands.Count > index && instruction.Operands[index] is PpcRegisterOperand operand)
            {
                register = NormalizeInstructionReg(operand.Name);
                return true;
            }

            register = string.Empty;
            return false;
        }

        static bool TryGetInstructionImm(PpcInstruction instruction, int index, out int immediate)
        {
            if (instruction.Operands.Count > index && instruction.Operands[index] is PpcImmediateOperand operand)
            {
                immediate = operand.Value;
                return true;
            }

            immediate = 0;
            return false;
        }

        static bool TryInstructionWritesDest(PpcInstruction instruction, out string destination)
        {
            destination = string.Empty;
            if (instruction.Operands.Count == 0 || instruction.Operands[0] is not PpcRegisterOperand operand)
            {
                return false;
            }

            var mnemonic = instruction.Mnemonic.ToLowerInvariant();
            if (mnemonic.StartsWith("st", StringComparison.Ordinal) ||
                mnemonic.StartsWith("b", StringComparison.Ordinal) ||
                mnemonic.StartsWith("cmp", StringComparison.Ordinal))
            {
                return false;
            }

            destination = NormalizeInstructionReg(operand.Name);
            return true;
        }

        static string NormalizeInstructionReg(string register) => register.ToLowerInvariant();
    }
}
