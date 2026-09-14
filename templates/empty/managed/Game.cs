using Vespera;

namespace VesperaGame;

// Intentionally minimal entry point for the Empty Project template. It gives a
// fresh project a real C# compilation/Play/export path without inventing game
// mechanics or scene content on the user's behalf.
public sealed class Game : Component
{
    public override void Start()
    {
        Log.Info("Empty Vespera project C# bootstrap started. Replace VesperaGame.Game with your own gameplay.");
    }
}
