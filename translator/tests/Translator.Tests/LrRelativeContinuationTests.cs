using System.Buffers.Binary;
using Translator.Core.Disassembly;
using Translator.Core.Loading;
using Translator.Core.Mods;
using Xunit;

namespace Translator.Tests;

public class LrRelativeContinuationTests
{
    [Theory]
    [InlineData(20, 40)]
    [InlineData(40, 20)]
    public void MutuallyExclusiveAdjustmentsKeepBothOffsets(int firstOffset, int secondOffset)
    {
        // Both arms start with the incoming LR and join at mtlr. Adding the
        // offsets together invents a continuation that neither arm can reach.
        var offsets = DiscoverOffsets(
            0x7FE802A6u, // +00: mflr r31
            0x2C030000u, // +04: cmpwi r3,0
            0x4182000Cu, // +08: beq +0x14
            AddiR31(firstOffset), // +0C: addi r31,r31,firstOffset
            0x48000008u, // +10: b +0x18
            AddiR31(secondOffset), // +14: addi r31,r31,secondOffset
            0x7FE803A6u, // +18: mtlr r31
            0x4E800020u);// +1C: blr

        Assert.Equal(new[] { 20, 40 }, offsets);
    }

    [Fact]
    public void NormalReturnArmDoesNotEraseSkipReturnAtSharedBlr()
    {
        // The normal arm writes the original LR; it must not overwrite the
        // other arm's LR + 20 in the analysis of the shared return.
        var offsets = DiscoverOffsets(
            0x7FE802A6u, // +00: mflr r31
            0x2C030000u, // +04: cmpwi r3,0
            0x41820010u, // +08: beq +0x18
            0x397F0014u, // +0C: addi r11,r31,20
            0x7D6803A6u, // +10: mtlr r11
            0x48000008u, // +14: b +0x1C
            0x7FE803A6u, // +18: mtlr r31
            0x4E800020u);// +1C: blr

        Assert.Equal(new[] { 20 }, offsets);
    }

    [Fact]
    public void ConditionalNormalReturnStillDiscoversSkipOnFallthrough()
    {
        var offsets = DiscoverOffsets(
            0x7FE802A6u, // mflr r31
            0x2C030000u, // cmpwi r3,0
            0x4D820020u, // beqlr
            0x3BFF0014u, // addi r31,r31,20
            0x7FE803A6u, // mtlr r31
            0x4E800020u);// blr

        Assert.Equal(new[] { 20 }, offsets);
    }

    [Fact]
    public void SavedNonvolatileLrSurvivesHelperCall()
    {
        // r31 survives a normal ABI call even though the call replaces LR.
        var offsets = DiscoverOffsets(
            0x7FE802A6u, // mflr r31
            0x48000101u, // bl helper outside this function
            0x3BFF0014u, // addi r31,r31,20
            0x7FE803A6u, // mtlr r31
            0x4E800020u);// blr

        Assert.Equal(new[] { 20 }, offsets);
    }

    [Fact]
    public void ReloadingSavedRegisterAfterMtlrDoesNotEraseSkipReturn()
    {
        // A hook epilogue restores the caller's r31 after committing its
        // adjusted return address to LR.
        var offsets = DiscoverOffsets(
            0x7FE802A6u, // mflr r31
            0x3BFF0014u, // addi r31,r31,20
            0x7FE803A6u, // mtlr r31
            0x83E10008u, // lwz r31,8(r1)
            0x4E800020u);// blr

        Assert.Equal(new[] { 20 }, offsets);
    }

    [Fact]
    public void UnknownLrWriteReplacesEarlierSkipReturn()
    {
        var offsets = DiscoverOffsets(
            0x7FE802A6u, // mflr r31
            0x3BFF0014u, // addi r31,r31,20
            0x7FE803A6u, // mtlr r31
            0x80010008u, // lwz r0,8(r1)
            0x7C0803A6u, // mtlr r0
            0x4E800020u);// blr

        Assert.Empty(offsets);
    }

    [Fact]
    public void UnadjustedRegisterReturnDoesNotAddAContinuation()
    {
        var offsets = DiscoverOffsets(
            0x7FE802A6u, // mflr r31
            0x7FE803A6u, // mtlr r31
            0x4E800020u);// blr

        Assert.Empty(offsets);
    }

    private static uint AddiR31(int offset) => 0x3BFF0000u | (uint)(offset & 0xFFFF);

    private static int[] DiscoverOffsets(params uint[] words)
    {
        const uint entry = 0x81800000u;
        var memory = new byte[words.Length * 4];
        for (var i = 0; i < words.Length; i++)
        {
            BinaryPrimitives.WriteUInt32BigEndian(memory.AsSpan(i * 4, 4), words[i]);
        }

        var range = AddressRange.FromStartAndSize(entry, (uint)memory.Length);
        var image = new ProgramImage(memory, range, range, default, "lr-continuation-test", entry);
        using var disassembler = new PpcDisassembler();
        // Use the production reachable-instruction traversal and ordering,
        // rather than handing the planner an artificial execution trace.
        var instructions = disassembler.DisassembleFunction(
            image, entry, maxInstructions: words.Length + 1, maxBytes: memory.Length);

        return ContinuationPlanner.DiscoverLrRelativeIndirectJumpOffsets(instructions)
            .Distinct().OrderBy(offset => offset).ToArray();
    }
}
