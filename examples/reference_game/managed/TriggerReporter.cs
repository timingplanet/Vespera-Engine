using Vespera;

namespace ReferenceGame.Scripts;

public sealed class TriggerReporter : Component
{
    private double _nextStayReport;

    public override void OnTriggerEnter(Entity other)
    {
        Log.Info($"TriggerReporter.OnTriggerEnter trigger={Entity.Id} other={other.Id}");
        _nextStayReport = Time.ElapsedTime + 1.0;
    }

    public override void OnTriggerStay(Entity other)
    {
        if (Time.ElapsedTime < _nextStayReport) return;
        _nextStayReport = Time.ElapsedTime + 1.0;
        Log.Info($"TriggerReporter.OnTriggerStay trigger={Entity.Id} other={other.Id}");
    }

    public override void OnTriggerExit(Entity other)
    {
        Log.Info($"TriggerReporter.OnTriggerExit trigger={Entity.Id} other={other.Id}");
    }
}
