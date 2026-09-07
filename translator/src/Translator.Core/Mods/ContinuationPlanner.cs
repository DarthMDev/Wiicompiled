using System.Buffers.Binary;
using System.Collections.Immutable;
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
        if (instructions.Count == 0)
        {
            yield break;
        }

        var indexByAddress = new Dictionary<uint, int>(instructions.Count);
        for (var i = 0; i < instructions.Count; i++)
        {
            indexByAddress.TryAdd(instructions[i].Address, i);
        }

        var visited = new HashSet<PathState>[instructions.Count];
        for (var i = 0; i < instructions.Count; i++)
        {
            visited[i] = new HashSet<PathState>();
        }

        var seenOffsets = new HashSet<int>();
        var worklist = new PriorityQueue<(int Index, PathState State), (uint Address, long Sequence)>(
            AddressSequenceComparer.Instance);
        long sequence = 0;
        const int MaxEvaluationSteps = 10000;
        var evaluationSteps = 0;

        void Enqueue(int targetIndex, PathState stateToEnqueue)
        {
            var targetAddress = instructions[targetIndex].Address;
            worklist.Enqueue((targetIndex, stateToEnqueue), (targetAddress, ++sequence));
        }

        int? GetFallthroughIndex(int currentIndex, PpcInstruction instruction)
        {
            if (indexByAddress.TryGetValue(instruction.EndAddress, out var nextIndex))
            {
                return nextIndex;
            }

            if (currentIndex + 1 < instructions.Count)
            {
                return currentIndex + 1;
            }

            return null;
        }

        Enqueue(0, PathState.Empty);

        while (worklist.Count > 0)
        {
            var (idx, state) = worklist.Dequeue();
            if (!visited[idx].Add(state))
            {
                continue;
            }

            if (++evaluationSteps > MaxEvaluationSteps)
            {
                break;
            }

            var instruction = instructions[idx];
            var mnemonic = instruction.Mnemonic.ToLowerInvariant();
            var nextState = state;

            if (mnemonic == "mflr" && TryGetInstructionReg(instruction, 0, out var lrDest))
            {
                nextState = nextState.WithLrOffset(lrDest, 0);
            }
            else if ((mnemonic == "mr" || mnemonic == "or") &&
                TryGetInstructionReg(instruction, 0, out var moveDest) &&
                TryGetInstructionReg(instruction, 1, out var moveSource) &&
                (mnemonic == "mr" ||
                 (instruction.Operands.Count >= 3 &&
                  instruction.Operands[2] is PpcRegisterOperand moveSource2 &&
                  string.Equals(NormalizeInstructionReg(moveSource2.Name), moveSource, StringComparison.OrdinalIgnoreCase))))
            {
                nextState = nextState.LrOffsets.TryGetValue(moveSource, out var sourceOffset)
                    ? nextState.WithLrOffset(moveDest, sourceOffset)
                    : nextState.WithoutLrOffset(moveDest);
            }
            else if (mnemonic == "addi" &&
                TryGetInstructionReg(instruction, 0, out var addDest) &&
                TryGetInstructionReg(instruction, 1, out var addBase) &&
                TryGetInstructionImm(instruction, 2, out var imm))
            {
                nextState = nextState.LrOffsets.TryGetValue(addBase, out var baseOffset)
                    ? nextState.WithLrOffset(addDest, checked(baseOffset + imm))
                    : nextState.WithoutLrOffset(addDest);
            }
            else if (mnemonic == "mtctr" && TryGetInstructionReg(instruction, 0, out var ctrSource))
            {
                var newCtrOffset = nextState.LrOffsets.TryGetValue(ctrSource, out var sourceOffset) ? sourceOffset : (int?)null;
                nextState = nextState.WithCtrOffset(newCtrOffset);
            }
            else if (mnemonic == "mtlr" && TryGetInstructionReg(instruction, 0, out var lrSource))
            {
                var newLrReturnOffset = nextState.LrOffsets.TryGetValue(lrSource, out var sourceOffset) ? sourceOffset : (int?)null;
                nextState = nextState.WithLrReturnOffset(newLrReturnOffset);
            }
            else
            {
                if (TryInstructionWritesDest(instruction, out var dest))
                {
                    nextState = nextState.WithoutLrOffset(dest);
                }
            }

            if (instruction.IsCall || mnemonic == "bl" || mnemonic == "blrl")
            {
                nextState = nextState.WithLrReturnOffset(null);
            }

            if (mnemonic == "bctr")
            {
                if (state.CtrOffset.HasValue && seenOffsets.Add(state.CtrOffset.Value))
                {
                    yield return state.CtrOffset.Value;
                }

                nextState = nextState.WithCtrOffset(null);
                if (instruction.BranchTargets.Count == 0)
                {
                    continue;
                }
            }

            var isReturn = instruction.IsReturn || mnemonic == "blr" || mnemonic == "bclr" ||
                (mnemonic.StartsWith("b", StringComparison.Ordinal) && mnemonic.EndsWith("lr", StringComparison.Ordinal));
            if (isReturn)
            {
                if (state.LrReturnOffset.HasValue && state.LrReturnOffset.Value != 0 && seenOffsets.Add(state.LrReturnOffset.Value))
                {
                    yield return state.LrReturnOffset.Value;
                }

                if (!instruction.IsConditionalBranch)
                {
                    continue;
                }
            }

            if (instruction.IsUnconditionalBranch)
            {
                foreach (var target in instruction.BranchTargets)
                {
                    if (indexByAddress.TryGetValue(target, out var targetIndex))
                    {
                        Enqueue(targetIndex, nextState);
                    }
                }
            }
            else if (instruction.IsConditionalBranch)
            {
                if (!isReturn)
                {
                    foreach (var target in instruction.BranchTargets)
                    {
                        if (indexByAddress.TryGetValue(target, out var targetIndex))
                        {
                            Enqueue(targetIndex, nextState);
                        }
                    }
                }

                var fallthrough = GetFallthroughIndex(idx, instruction);
                if (fallthrough.HasValue)
                {
                    Enqueue(fallthrough.Value, nextState);
                }
            }
            else
            {
                var fallthrough = GetFallthroughIndex(idx, instruction);
                if (fallthrough.HasValue)
                {
                    Enqueue(fallthrough.Value, nextState);
                }
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

    private sealed class PathState : IEquatable<PathState>
    {
        public ImmutableDictionary<string, int> LrOffsets { get; }
        public int? CtrOffset { get; }
        public int? LrReturnOffset { get; }

        public PathState(ImmutableDictionary<string, int> lrOffsets, int? ctrOffset, int? lrReturnOffset)
        {
            LrOffsets = lrOffsets;
            CtrOffset = ctrOffset;
            LrReturnOffset = lrReturnOffset;
        }

        public static readonly PathState Empty = new(
            ImmutableDictionary<string, int>.Empty.WithComparers(StringComparer.OrdinalIgnoreCase),
            null,
            null);

        public PathState WithLrOffset(string register, int offset) =>
            new(LrOffsets.SetItem(register, offset), CtrOffset, LrReturnOffset);

        public PathState WithoutLrOffset(string register) =>
            LrOffsets.ContainsKey(register)
                ? new(LrOffsets.Remove(register), CtrOffset, LrReturnOffset)
                : this;

        public PathState WithCtrOffset(int? ctrOffset) =>
            new(LrOffsets, ctrOffset, LrReturnOffset);

        public PathState WithLrReturnOffset(int? lrReturnOffset) =>
            new(LrOffsets, CtrOffset, lrReturnOffset);

        public bool Equals(PathState? other)
        {
            if (ReferenceEquals(this, other)) return true;
            if (other is null) return false;
            if (CtrOffset != other.CtrOffset || LrReturnOffset != other.LrReturnOffset) return false;
            if (LrOffsets.Count != other.LrOffsets.Count) return false;
            foreach (var (k, v) in LrOffsets)
            {
                if (!other.LrOffsets.TryGetValue(k, out var otherV) || v != otherV)
                {
                    return false;
                }
            }
            return true;
        }

        public override bool Equals(object? obj) => obj is PathState other && Equals(other);

        public override int GetHashCode()
        {
            var hash = new HashCode();
            hash.Add(CtrOffset);
            hash.Add(LrReturnOffset);
            hash.Add(LrOffsets.Count);
            var regHash = 0;
            foreach (var (k, v) in LrOffsets)
            {
                regHash ^= HashCode.Combine(StringComparer.OrdinalIgnoreCase.GetHashCode(k), v);
            }
            hash.Add(regHash);
            return hash.ToHashCode();
        }
    }

    private sealed class AddressSequenceComparer : IComparer<(uint Address, long Sequence)>
    {
        public static readonly AddressSequenceComparer Instance = new();

        public int Compare((uint Address, long Sequence) x, (uint Address, long Sequence) y)
        {
            var cmp = x.Address.CompareTo(y.Address);
            return cmp != 0 ? cmp : x.Sequence.CompareTo(y.Sequence);
        }
    }
}
