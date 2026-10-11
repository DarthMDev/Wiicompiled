using System.Buffers.Binary;
using Translator.Core.Loading;
using Translator.Core.Parsing.Kamek;

namespace Translator.Core.Mods.Mkwii;

public static class RetroRewindRegionPatches
{
    private const uint PalVoiceHook = 0x808DBCD8;
    private const uint UsVoiceHook = 0x808D7580;

    // CustomCharacterVoice.cpp's HoldRosalinaLumaSound pointer retains its PAL address
    // in the shipped US chunk. The clean US REL's relocated vtable identifies the slot;
    // these surrounding entries distinguish it from other tables using HoldSoundLimited.
    private static readonly (uint Address, uint Target)[] UsVoiceTable =
    [
        (0x808D7540, 0x806FC03C),
        (0x808D7544, 0x806FBC94),
        (0x808D7570, 0x806FBC3C),
        (0x808D7574, 0x806FBC34),
        (0x808D7578, 0x806FA3A8),
        (0x808D757C, 0x806FAA04),
        (UsVoiceHook, 0x806FAB44),
        (0x808D7584, 0x806FAD58),
        (0x808D7588, 0x806FA7B0),
        (0x808D758C, 0x806FA83C),
        (0x808D7590, 0x806FA8D0),
        (0x808D75BC, 0x808611E8),
        (0x808D75C0, 0x806FA824)
    ];

    public static KamekChunk CorrectUsVoiceHook(KamekChunk chunk, string? gameId, ProgramImage image)
    {
        if (!string.Equals(gameId, "RMCE01", StringComparison.OrdinalIgnoreCase))
            return chunk;

        var matching = chunk.Commands.Where(c => c.AddressIsAbsolute && c.Address == PalVoiceHook).ToArray();
        if (matching.Length == 0)
            return chunk;
        if (matching.Length != 1 || matching[0].Id != KamekCommandId.Addr32 ||
            matching[0].Arguments.Count != 1 || matching[0].Arguments[0] >= chunk.CodeSize ||
            chunk.Commands.Any(c => c.AddressIsAbsolute && c.Address == UsVoiceHook))
            throw new InvalidDataException("Unexpected Retro Rewind US voice hook commands; cannot correct the PAL address.");

        foreach (var (address, target) in UsVoiceTable)
        {
            if (!image.Contains(address, 4) ||
                BinaryPrimitives.ReadUInt32BigEndian(image.Memory.AsSpan(image.GetOffset(address, 4), 4)) != target)
                throw new InvalidDataException($"Retro Rewind US voice vtable check failed at 0x{address:X8}.");
        }

        return new KamekChunk(chunk.Index, chunk.FileOffset, chunk.BssSize, chunk.CodeSize,
            chunk.CtorStart, chunk.CtorEnd, chunk.ChunkSize, chunk.CodeBlob,
            chunk.Commands.Select(c => ReferenceEquals(c, matching[0]) ? c with { Address = UsVoiceHook } : c).ToArray());
    }
}
