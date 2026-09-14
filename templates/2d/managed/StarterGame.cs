using Vespera;

namespace VesperaGame;

// Experimental screen-space 2D starter for Vespera 1.0.
//
// Vespera 1.0 does not pretend to have a complete orthographic world/editor
// workflow. Instead, this template gives 2D/UI-heavy games a genuinely distinct
// starting point: RmlUi is the 2D canvas and project-owned C# drives a small game
// loop through Vespera's public input/UI API. Replace the coloured player/goal divs
// with images, panels, cards, HUD widgets, or other RmlUi content as your game grows.
public sealed class StarterGame : Component
{
    private const float ArenaWidth = 720.0f;
    private const float ArenaHeight = 405.0f;
    private const float PlayerSize = 46.0f;
    private const float GoalSize = 30.0f;

    [Expose("Move Speed")]
    [Range(60.0f, 800.0f)]
    public float MoveSpeed = 280.0f;

    private UiElement? _player;
    private UiElement? _goal;
    private UiElement? _score;
    private UiElement? _status;
    private UiElement? _reset;

    private float _playerX;
    private float _playerY;
    private float _goalX;
    private float _goalY;
    private int _scoreValue;
    private int _goalIndex;

    // Deterministic positions keep the starter easy to understand and make the
    // same project deterministic in Editor Play and exported builds.
    private static readonly (float X, float Y)[] GoalPositions =
    {
        (585.0f, 82.0f),
        (110.0f, 285.0f),
        (530.0f, 300.0f),
        (335.0f, 105.0f),
        (620.0f, 245.0f),
        (175.0f, 120.0f),
    };

    public override void Start()
    {
        _player = UI.Find("player");
        _goal = UI.Find("goal");
        _score = UI.Find("score");
        _status = UI.Find("starter-status");
        _reset = UI.Find("reset");

        ResetGame();
        Log.Info("2D/UI starter is running. Move with WASD or the arrow keys.");
    }

    public override void Update(float deltaTime)
    {
        var x = Input.Value("move_horizontal");
        var y = Input.Value("move_vertical");

        // Normalize diagonals so the starter does not move faster at 45 degrees.
        var length = MathF.Sqrt(x * x + y * y);
        if (length > 1.0f)
        {
            x /= length;
            y /= length;
        }

        _playerX = Math.Clamp(_playerX + x * MoveSpeed * deltaTime, 0.0f, ArenaWidth - PlayerSize);
        _playerY = Math.Clamp(_playerY + y * MoveSpeed * deltaTime, 0.0f, ArenaHeight - PlayerSize);
        ApplyPlayerPosition();

        if (_reset?.Clicked ?? false)
            ResetGame();

        if (OverlapsGoal())
        {
            _scoreValue++;
            _goalIndex = (_goalIndex + 1) % GoalPositions.Length;
            PlaceGoal(GoalPositions[_goalIndex].X, GoalPositions[_goalIndex].Y);
            UpdateHud("Nice. The whole playable area is ordinary RmlUi driven by C#.");
        }
    }

    private bool OverlapsGoal()
    {
        return _playerX < _goalX + GoalSize
            && _playerX + PlayerSize > _goalX
            && _playerY < _goalY + GoalSize
            && _playerY + PlayerSize > _goalY;
    }

    private void ResetGame()
    {
        _scoreValue = 0;
        _goalIndex = 0;
        _playerX = (ArenaWidth - PlayerSize) * 0.5f;
        _playerY = (ArenaHeight - PlayerSize) * 0.5f;
        ApplyPlayerPosition();
        PlaceGoal(GoalPositions[0].X, GoalPositions[0].Y);
        UpdateHud("Move the blue square with WASD or the arrow keys and collect the gold target.");
    }

    private void ApplyPlayerPosition()
    {
        if (_player is null) return;
        _player.SetProperty("left", $"{_playerX:0.0}px");
        _player.SetProperty("top", $"{_playerY:0.0}px");
    }

    private void PlaceGoal(float x, float y)
    {
        _goalX = x;
        _goalY = y;
        if (_goal is null) return;
        _goal.SetProperty("left", $"{_goalX:0.0}px");
        _goal.SetProperty("top", $"{_goalY:0.0}px");
    }

    private void UpdateHud(string message)
    {
        if (_score is not null)
            _score.Text = $"Score {_scoreValue}";
        if (_status is not null)
            _status.Text = message;
    }
}
