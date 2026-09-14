using Vespera;

namespace VesperaGame;

// Small project-owned controller used by the 3D / 2.5D template. The shared
// vespera_player owns no hidden gameplay: this script reads the project's input
// map and moves the authored starter actor through the public component API.
public sealed class StarterController : Component
{
    [Expose("Move Speed")]
    [Range(0.25f, 12.0f)]
    public float MoveSpeed = 3.0f;

    private UiElement? _hint;

    public override void Start()
    {
        _hint = UI.Find("starter-hint");
        UpdateHint();
        Log.Info("3D starter controller is running. Use WASD; hold Shift to sprint.");
    }

    public override void Update(float deltaTime)
    {
        var x = Input.Value("move_right");
        var z = Input.Value("move_forward");
        if (MathF.Abs(x) < 0.001f && MathF.Abs(z) < 0.001f) return;

        var speed = MoveSpeed * (Input.Down("sprint") ? 1.8f : 1.0f);
        var delta = new Vector3(x * speed * deltaTime, 0.0f, z * speed * deltaTime);
        if (Entity.CylinderCollider is { } collider)
            collider.MoveBy(delta);
        else
        {
            var p = Entity.Position;
            Entity.Position = new Vector3(p.X + delta.X, p.Y, p.Z + delta.Z);
        }
        UpdateHint();
    }

    private void UpdateHint()
    {
        if (_hint is null) return;
        var p = Entity.Position;
        _hint.Text = $"WASD moves the starter cube · Shift sprints · X {p.X:0.0}  Z {p.Z:0.0}";
    }
}
