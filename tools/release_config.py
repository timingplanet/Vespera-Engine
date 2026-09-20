"""Pinned inputs used to assemble official Vespera binary releases."""

# Vespera ships a private SDK because Build C# must work on a clean machine.
# Keep this exact for reproducible release archives; normal source builds may use
# any supported SDK discovered by VesperaBuilder.
DOTNET_CHANNEL = "10.0"
DOTNET_SDK_VERSION = "10.0.401"
